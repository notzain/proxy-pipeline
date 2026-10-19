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

UdsOutputChannel::UdsOutputChannel(std::string destPath)
    : m_destPath(std::move(destPath))
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

bool UdsOutputChannel::send(std::span<const std::byte> message)
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
        for (int retry = 0; retry < 10 && sent < 0; ++retry) {
            std::this_thread::yield();
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

} // namespace Proxy::Backend::Uds
