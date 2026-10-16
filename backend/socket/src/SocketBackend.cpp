#include "backend/socket/SocketBackend.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace Proxy::Backend::Socket {

SocketBackend::SocketBackend() = default;

SocketBackend::~SocketBackend()
{
    closeSocket();
}

void SocketBackend::closeSocket()
{
    if (m_socketFd >= 0) {
        ::close(m_socketFd);
        m_socketFd = -1;
    }
}

void SocketBackend::openAndBindSocket()
{
    closeSocket();

    m_socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socketFd < 0) {
        spdlog::error("[SocketBackend] Failed to create UDP socket: {}", std::strerror(errno));
        return;
    }

    int reuse = 1;
    ::setsockopt(m_socketFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr { };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(m_port);

    if (m_host == "0.0.0.0" || m_host.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        ::inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr);
    }

    if (::bind(m_socketFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        spdlog::error("[SocketBackend] Failed to bind UDP socket to {}:{} ({})", m_host, m_port, std::strerror(errno));
        closeSocket();
        return;
    }

    spdlog::info("[SocketBackend] Listening on UDP {}:{}", m_host, m_port);
}

void SocketBackend::configure(const ::Backend::IConfig& config)
{
    m_host = config.getOr<std::string>("socket.host", "0.0.0.0");
    m_port = config.getOr<std::uint16_t>("socket.port", 8080);

    openAndBindSocket();
}

std::optional<::Backend::IBackend::Buffer> SocketBackend::nextMessage(Timeout timeout)
{
    if (m_socketFd < 0) {
        openAndBindSocket();
        if (m_socketFd < 0) {
            return std::nullopt;
        }
    }

    pollfd pfd { };
    pfd.fd = m_socketFd;
    pfd.events = POLLIN;

    int ret = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
    if (ret > 0 && (pfd.revents & POLLIN)) {
        constexpr std::size_t maxPacketSize = 65535;
        Buffer buffer(maxPacketSize);

        sockaddr_in senderAddr { };
        socklen_t senderLen = sizeof(senderAddr);

        ssize_t bytesRead = ::recvfrom(
            m_socketFd,
            reinterpret_cast<char*>(buffer.data()),
            buffer.size(),
            0,
            reinterpret_cast<sockaddr*>(&senderAddr),
            &senderLen);

        if (bytesRead > 0) {
            buffer.resize(static_cast<std::size_t>(bytesRead));
            return buffer;
        }
    }

    return std::nullopt;
}

} // namespace Proxy::Backend::Socket

namespace Backend {

std::unique_ptr<IBackend> createBackend()
{
    return std::make_unique<Proxy::Backend::Socket::SocketBackend>();
}

} // namespace Backend
