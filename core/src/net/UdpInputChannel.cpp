#include "core/net/UdpInputChannel.hpp"
#include "backend/ISocketPoller.hpp"
#include "log/Log.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace Core::Net {

UdpInputChannel::UdpInputChannel(std::string host, std::uint16_t port, std::size_t receiveBufferSize)
    : m_host(std::move(host))
    , m_port(port)
    , m_receiveBufferSize(receiveBufferSize)
    , m_poller(::Backend::createSocketPoller())
{
    openAndBindSocket();
}

UdpInputChannel::~UdpInputChannel()
{
    closeSocket();
}

UdpInputChannel::UdpInputChannel(UdpInputChannel&& other) noexcept
    : m_host(std::move(other.m_host))
    , m_port(other.m_port)
    , m_receiveBufferSize(other.m_receiveBufferSize)
    , m_socketFd(other.m_socketFd)
    , m_poller(std::move(other.m_poller))
{
    other.m_socketFd = -1;
}

UdpInputChannel& UdpInputChannel::operator=(UdpInputChannel&& other) noexcept
{
    if (this != &other) {
        closeSocket();
        m_host = std::move(other.m_host);
        m_port = other.m_port;
        m_receiveBufferSize = other.m_receiveBufferSize;
        m_socketFd = other.m_socketFd;
        m_poller = std::move(other.m_poller);
        other.m_socketFd = -1;
    }
    return *this;
}

void UdpInputChannel::closeSocket()
{
    if (m_socketFd >= 0) {
        ::close(m_socketFd);
        m_socketFd = -1;
    }
}

void UdpInputChannel::openAndBindSocket()
{
    closeSocket();

    m_socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socketFd < 0) {
        Proxy::Log::error("[UdpInputChannel] Failed to create UDP socket: {}", std::strerror(errno));
        return;
    }

    int reuse = 1;
    ::setsockopt(m_socketFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    ::setsockopt(m_socketFd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    int rcvbuf = static_cast<int>(m_receiveBufferSize);
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
        Proxy::Log::error("[UdpInputChannel] Failed to bind UDP socket to {}:{} ({})", m_host, m_port, std::strerror(errno));
        closeSocket();
        return;
    }

    Proxy::Log::info("[UdpInputChannel] Bound and listening on UDP {}:{}", m_host, m_port);
}

std::optional<Backend::IInputChannel::Buffer> UdpInputChannel::receive(Timeout timeout)
{
    Buffer buffer(65535);
    std::size_t bytes = receiveInto(std::span<std::byte>(buffer.data(), buffer.size()), timeout);
    if (bytes == 0) {
        return std::nullopt;
    }
    buffer.resize(bytes);
    return buffer;
}

std::size_t UdpInputChannel::receiveInto(std::span<std::byte> destination, Timeout timeout)
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

std::size_t UdpInputChannel::receiveBatch(
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

int UdpInputChannel::getNativeSocket() const noexcept
{
    return m_socketFd;
}

const std::string& UdpInputChannel::getHost() const noexcept
{
    return m_host;
}

std::uint16_t UdpInputChannel::getPort() const noexcept
{
    return m_port;
}

} // namespace Core::Net
