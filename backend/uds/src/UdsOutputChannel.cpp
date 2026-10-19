#include "backend/uds/UdsOutputChannel.hpp"
#include "log/Log.hpp"

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
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

    int sndbuf = 4 * 1024 * 1024; // 4MB send buffer
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
    if (::connect(m_socketFd, reinterpret_cast<const sockaddr*>(&m_destAddr), sizeof(m_destAddr)) < 0) {
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

    ssize_t sent = ::send(
        m_socketFd,
        reinterpret_cast<const char*>(message.data()),
        message.size(),
        0);

    if (sent < 0) {
        // Fallback or retry with sendto (connect might have been deferred)
        sent = ::sendto(
            m_socketFd,
            reinterpret_cast<const char*>(message.data()),
            message.size(),
            0,
            reinterpret_cast<const sockaddr*>(&m_destAddr),
            sizeof(m_destAddr));
    }

    if (sent < 0) {
        Proxy::Log::error("[UdsOutputChannel] Failed to send {} bytes to {} ({})", message.size(), m_destPath, std::strerror(errno));
        return false;
    }

    Proxy::Log::debug("[UdsOutputChannel] Sent {} bytes to {}", sent, m_destPath);
    return true;
}

} // namespace Proxy::Backend::Uds
