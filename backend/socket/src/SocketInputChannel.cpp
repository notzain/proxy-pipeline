#include "backend/socket/SocketInputChannel.hpp"
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

    int rcvbuf = 4 * 1024 * 1024; // 4MB receive buffer for high-throughput bursts
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

    pollfd pfd { };
    pfd.fd = m_socketFd;
    pfd.events = POLLIN;

    int timeoutMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    int ret = ::poll(&pfd, 1, timeoutMs);

    if (ret > 0 && (pfd.revents & POLLIN)) {
        sockaddr_in clientAddr { };
        socklen_t clientLen = sizeof(clientAddr);

        ssize_t bytesRead = ::recvfrom(
            m_socketFd,
            reinterpret_cast<char*>(destination.data()),
            destination.size(),
            0,
            reinterpret_cast<sockaddr*>(&clientAddr),
            &clientLen);

        if (bytesRead > 0) {
            char clientIp[INET_ADDRSTRLEN];
            ::inet_ntop(AF_INET, &clientAddr.sin_addr, clientIp, sizeof(clientIp));
            Proxy::Log::debug("[SocketInputChannel] Received {} bytes from {}:{}", bytesRead, clientIp, ntohs(clientAddr.sin_port));
            return static_cast<std::size_t>(bytesRead);
        }
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
