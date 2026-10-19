#include "backend/socket/SocketInputChannel.hpp"
#include "backend/ISocketPoller.hpp"
#include "log/Log.hpp"
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace Proxy::Backend::Socket {

SocketInputChannel::SocketInputChannel(std::string host, std::uint16_t port)
    : m_host(std::move(host))
    , m_port(port)
    , m_poller(::Backend::createSocketPoller())
{
    openAndBindSocket();
}

SocketInputChannel::~SocketInputChannel()
{
    closeSocket();
}

void SocketInputChannel::closeSocket()
{
    if (m_socketFd >= 0) {
        ::close(m_socketFd);
        m_socketFd = -1;
    }
}

void SocketInputChannel::openAndBindSocket()
{
    closeSocket();

    m_socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socketFd < 0) {
        Proxy::Log::error("[SocketInputChannel] Failed to create UDP socket: {}", std::strerror(errno));
        return;
    }

    int reuse = 1;
    ::setsockopt(m_socketFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    int rcvbuf = 8 * 1024 * 1024; // 8MB receive buffer for maximum throughput burst absorption
    ::setsockopt(m_socketFd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    sockaddr_in addr { };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);

    if (m_host == "0.0.0.0" || m_host.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        ::inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr);
    }

    if (::bind(m_socketFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        Proxy::Log::error("[SocketInputChannel] Failed to bind UDP socket to {}:{} ({})", m_host, m_port, std::strerror(errno));
        closeSocket();
        return;
    }

    Proxy::Log::info("[SocketInputChannel] Listening on UDP {}:{}", m_host, m_port);
}

std::size_t SocketInputChannel::receiveInto(std::span<std::byte> destination, Timeout timeout)
{
    if (m_socketFd < 0) {
        openAndBindSocket();
        if (m_socketFd < 0) {
            return 0;
        }
    }

    if (m_poller) {
        return m_poller->receive(m_socketFd, destination, timeout);
    }

    return 0;
}

std::size_t SocketInputChannel::receiveBatch(
    std::span<const std::span<std::byte>> destinations,
    std::span<std::size_t> outSizes,
    Timeout timeout)
{
    if (m_socketFd < 0) {
        openAndBindSocket();
        if (m_socketFd < 0) {
            return 0;
        }
    }

    if (m_poller) {
        return m_poller->receiveBatch(m_socketFd, destinations, outSizes, timeout);
    }

    return 0;
}

std::optional<::Backend::IInputChannel::Buffer> SocketInputChannel::receive(Timeout timeout)
{
    thread_local std::array<std::byte, 65535> scratch;
    std::size_t bytesRead = receiveInto(scratch, timeout);
    if (bytesRead > 0) {
        return Buffer(scratch.begin(), scratch.begin() + bytesRead);
    }
    return std::nullopt;
}

} // namespace Proxy::Backend::Socket
