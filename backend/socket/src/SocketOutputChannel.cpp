#include "backend/socket/SocketOutputChannel.hpp"
#include "log/Log.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace Proxy::Backend::Socket {

SocketOutputChannel::SocketOutputChannel(std::string destHost, std::uint16_t destPort)
    : m_destHost(std::move(destHost))
    , m_destPort(destPort)
{
    openSocket();
}

SocketOutputChannel::~SocketOutputChannel()
{
    closeSocket();
}

void SocketOutputChannel::closeSocket()
{
    if (m_socketFd >= 0) {
        ::close(m_socketFd);
        m_socketFd = -1;
    }
}

void SocketOutputChannel::openSocket()
{
    closeSocket();
    m_socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socketFd < 0) {
        Proxy::Log::error("[SocketOutputChannel] Failed to create UDP socket: {}", std::strerror(errno));
        return;
    }

    int sndbuf = 8 * 1024 * 1024; // 8MB send buffer for maximum throughput burst transmission
    ::setsockopt(m_socketFd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    std::memset(&m_destAddr, 0, sizeof(m_destAddr));
    m_destAddr.sin_family = AF_INET;
    m_destAddr.sin_port = htons(m_destPort);

    if (::inet_pton(AF_INET, m_destHost.c_str(), &m_destAddr.sin_addr) <= 0) {
        Proxy::Log::error("[SocketOutputChannel] Invalid destination IP address: {}", m_destHost);
    }

    Proxy::Log::info("[SocketOutputChannel] Target configured for UDP {}:{}", m_destHost, m_destPort);

    // Connect socket to destination address to eliminate per-packet kernel route lookups
    if (::connect(m_socketFd, reinterpret_cast<const sockaddr*>(&m_destAddr), sizeof(m_destAddr)) == 0) {
        m_connected = true;
    } else {
        m_connected = false;
        Proxy::Log::warn("[SocketOutputChannel] Failed to connect UDP socket to {}:{} ({})", m_destHost, m_destPort, std::strerror(errno));
    }
}

bool SocketOutputChannel::send(std::span<const std::byte> message)
{
    if (m_socketFd < 0) {
        openSocket();
        if (m_socketFd < 0) {
            return false;
        }
    }

    ssize_t sent = -1;
    if (m_connected.load(std::memory_order_relaxed)) {
        sent = ::send(
            m_socketFd,
            reinterpret_cast<const char*>(message.data()),
            message.size(),
            0);

        // If an ICMP Port Unreachable or route invalidation occurred, refresh connection
        if (sent < 0 && (errno == ECONNREFUSED || errno == ENETUNREACH || errno == ENETDOWN)) {
            if (::connect(m_socketFd, reinterpret_cast<const sockaddr*>(&m_destAddr), sizeof(m_destAddr)) == 0) {
                sent = ::send(
                    m_socketFd,
                    reinterpret_cast<const char*>(message.data()),
                    message.size(),
                    0);
            }
        }
    } else {
        // Attempt lazy connect
        int connRes = ::connect(m_socketFd, reinterpret_cast<const sockaddr*>(&m_destAddr), sizeof(m_destAddr));
        if (connRes == 0 || errno == EISCONN) {
            m_connected.store(true, std::memory_order_relaxed);
            sent = ::send(
                m_socketFd,
                reinterpret_cast<const char*>(message.data()),
                message.size(),
                0);
        } else {
            sent = ::sendto(
                m_socketFd,
                reinterpret_cast<const char*>(message.data()),
                message.size(),
                0,
                reinterpret_cast<const sockaddr*>(&m_destAddr),
                sizeof(m_destAddr));
            if (sent < 0 && errno == EISCONN) {
                m_connected.store(true, std::memory_order_relaxed);
                sent = ::send(
                    m_socketFd,
                    reinterpret_cast<const char*>(message.data()),
                    message.size(),
                    0);
            }
        }
    }

    if (sent < 0 && (errno == ENOBUFS || errno == EAGAIN)) {
        // Socket buffer full under heavy burst; give receiver thread a chance to drain
        for (int retry = 0; retry < 50 && sent < 0; ++retry) {
            if (retry < 10) {
                std::this_thread::yield();
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(20));
            }
            if (m_connected.load(std::memory_order_relaxed)) {
                sent = ::send(
                    m_socketFd,
                    reinterpret_cast<const char*>(message.data()),
                    message.size(),
                    0);
            } else {
                sent = ::sendto(
                    m_socketFd,
                    reinterpret_cast<const char*>(message.data()),
                    message.size(),
                    0,
                    reinterpret_cast<const sockaddr*>(&m_destAddr),
                    sizeof(m_destAddr));
            }
        }
    } else if (sent < 0 && errno == EDESTADDRREQ) {
        if (::connect(m_socketFd, reinterpret_cast<const sockaddr*>(&m_destAddr), sizeof(m_destAddr)) == 0) {
            m_connected.store(true, std::memory_order_relaxed);
            sent = ::send(
                m_socketFd,
                reinterpret_cast<const char*>(message.data()),
                message.size(),
                0);
        } else {
            m_connected.store(false, std::memory_order_relaxed);
            sent = ::sendto(
                m_socketFd,
                reinterpret_cast<const char*>(message.data()),
                message.size(),
                0,
                reinterpret_cast<const sockaddr*>(&m_destAddr),
                sizeof(m_destAddr));
        }
    }

    if (sent < 0) {
        Proxy::Log::error("[SocketOutputChannel] Failed to send {} bytes to {}:{} ({})", message.size(), m_destHost, m_destPort, std::strerror(errno));
        return false;
    }

    Proxy::Log::debug("[SocketOutputChannel] Sent {} bytes to {}:{}", sent, m_destHost, m_destPort);
    return true;
}

std::size_t SocketOutputChannel::sendBatch(std::span<const std::span<const std::byte>> messages)
{
    if (messages.empty()) {
        return 0;
    }

#if defined(__linux__)
    constexpr std::size_t maxBatch = 32;
    std::size_t totalSent = 0;

    for (std::size_t offset = 0; offset < messages.size(); offset += maxBatch) {
        std::size_t chunk = std::min(maxBatch, messages.size() - offset);
        struct mmsghdr msgvec[maxBatch];
        struct iovec iov[maxBatch];
        std::memset(msgvec, 0, sizeof(msgvec[0]) * chunk);

        for (std::size_t i = 0; i < chunk; ++i) {
            const auto& msg = messages[offset + i];
            iov[i].iov_base = const_cast<char*>(reinterpret_cast<const char*>(msg.data()));
            iov[i].iov_len = msg.size();

            msgvec[i].msg_hdr.msg_iov = &iov[i];
            msgvec[i].msg_hdr.msg_iovlen = 1;
            if (!m_connected.load(std::memory_order_relaxed)) {
                msgvec[i].msg_hdr.msg_name = &m_destAddr;
                msgvec[i].msg_hdr.msg_namelen = sizeof(m_destAddr);
            }
        }

        int sentCount = ::sendmmsg(m_socketFd, msgvec, static_cast<unsigned int>(chunk), 0);
        if (sentCount > 0) {
            totalSent += static_cast<std::size_t>(sentCount);
            if (static_cast<std::size_t>(sentCount) < chunk) {
                break;
            }
        } else if (sentCount < 0 && (errno == ENOBUFS || errno == EAGAIN)) {
            // Buffer saturation under heavy burst; fallback to single retry send
            for (std::size_t i = 0; i < chunk; ++i) {
                if (send(messages[offset + i])) {
                    ++totalSent;
                }
            }
        } else {
            break;
        }
    }
    return totalSent;
#else
    std::size_t count = 0;
    for (const auto& msg : messages) {
        if (send(msg)) {
            ++count;
        }
    }
    return count;
#endif
}

} // namespace Proxy::Backend::Socket
