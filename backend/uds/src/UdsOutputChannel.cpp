#include "backend/uds/UdsOutputChannel.hpp"
#include "log/Log.hpp"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace Proxy::Backend::Uds {

UdsOutputChannel::UdsOutputChannel(std::string name, std::string destPath)
    : ::Backend::IOutputChannel(std::move(name))
    , m_destPath(std::move(destPath))
{
    openSocket();
}

UdsOutputChannel::~UdsOutputChannel()
{
    closeSocket();
}

void UdsOutputChannel::closeSocket()
{
    if (m_socketFd >= 0) {
        ::close(m_socketFd);
        m_socketFd = -1;
    }
}

void UdsOutputChannel::openSocket()
{
    closeSocket();
    m_socketFd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    if (m_socketFd < 0) {
        Proxy::Log::error("[UdsOutputChannel] Failed to create UDS socket: {}", std::strerror(errno));
        return;
    }

    int sndbuf = 8 * 1024 * 1024; // 8MB send buffer
    ::setsockopt(m_socketFd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    if (m_destPath.size() >= sizeof(sockaddr_un::sun_path)) {
        Proxy::Log::error("[UdsOutputChannel] Destination path too long: {}", m_destPath);
        return;
    }

    std::memset(&m_destAddr, 0, sizeof(m_destAddr));
    m_destAddr.sun_family = AF_UNIX;
    std::strncpy(m_destAddr.sun_path, m_destPath.c_str(), sizeof(m_destAddr.sun_path) - 1);

    Proxy::Log::info("[UdsOutputChannel] Target configured for UDS path {}", m_destPath);

    // Connect socket to destination address to eliminate per-packet kernel routing checks
    if (::connect(m_socketFd, reinterpret_cast<const sockaddr*>(&m_destAddr), sizeof(m_destAddr)) == 0) {
        m_connected = true;
    } else {
        m_connected = false;
        // May fail if receiver is not bound yet; fallback to sendto will work
        Proxy::Log::debug("[UdsOutputChannel] Non-blocking initial connect deferred: {}", std::strerror(errno));
    }
}

bool UdsOutputChannel::send(Common::Compat::span<const std::byte> message)
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
        // Socket was marked connected but connection was broken or unbound; re-connect and retry
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
        Proxy::Log::error("[UdsOutputChannel] Failed to send {} bytes to {} ({})", message.size(), m_destPath, std::strerror(errno));
        return false;
    }

    Proxy::Log::debug("[UdsOutputChannel] Sent {} bytes to {}", sent, m_destPath);
    return true;
}

std::size_t UdsOutputChannel::sendBatch(Common::Compat::span<const Common::Compat::span<const std::byte>> messages)
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

} // namespace Proxy::Backend::Uds
