#include "core/net/UdpOutputChannel.hpp"
#include "log/Log.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace Core::Net {

UdpOutputChannel::UdpOutputChannel(std::string defaultHost, std::uint16_t defaultPort, std::size_t sendBufferSize)
    : m_defaultHost(std::move(defaultHost))
    , m_defaultPort(defaultPort)
    , m_sendBufferSize(sendBufferSize)
{
    openSocket();
}

UdpOutputChannel::~UdpOutputChannel()
{
    closeSocket();
}

UdpOutputChannel::UdpOutputChannel(UdpOutputChannel&& other) noexcept
    : m_defaultHost(std::move(other.m_defaultHost))
    , m_defaultPort(other.m_defaultPort)
    , m_sendBufferSize(other.m_sendBufferSize)
    , m_socketFd(other.m_socketFd)
    , m_defaultAddr(other.m_defaultAddr)
    , m_hasDefaultAddr(other.m_hasDefaultAddr)
{
    other.m_socketFd = -1;
}

UdpOutputChannel& UdpOutputChannel::operator=(UdpOutputChannel&& other) noexcept
{
    if (this != &other) {
        closeSocket();
        m_defaultHost = std::move(other.m_defaultHost);
        m_defaultPort = other.m_defaultPort;
        m_sendBufferSize = other.m_sendBufferSize;
        m_socketFd = other.m_socketFd;
        m_defaultAddr = other.m_defaultAddr;
        m_hasDefaultAddr = other.m_hasDefaultAddr;
        other.m_socketFd = -1;
    }
    return *this;
}

void UdpOutputChannel::closeSocket()
{
    if (m_socketFd >= 0) {
        ::close(m_socketFd);
        m_socketFd = -1;
    }
}

void UdpOutputChannel::openSocket()
{
    closeSocket();
    m_socketFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (m_socketFd < 0) {
        Proxy::Log::error("[UdpOutputChannel] Failed to create UDP socket: {}", std::strerror(errno));
        return;
    }

    int sndbuf = static_cast<int>(m_sendBufferSize);
    ::setsockopt(m_socketFd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    if (!m_defaultHost.empty() && m_defaultPort != 0) {
        std::memset(&m_defaultAddr, 0, sizeof(m_defaultAddr));
        m_defaultAddr.sin_family = AF_INET;
        m_defaultAddr.sin_port = htons(m_defaultPort);
        if (::inet_pton(AF_INET, m_defaultHost.c_str(), &m_defaultAddr.sin_addr) > 0) {
            m_hasDefaultAddr = true;
            Proxy::Log::info("[UdpOutputChannel] Configured default target UDP {}:{}", m_defaultHost, m_defaultPort);
        } else {
            Proxy::Log::error("[UdpOutputChannel] Invalid default target IP: {}", m_defaultHost);
        }
    }
}

bool UdpOutputChannel::send(std::span<const std::byte> message)
{
    if (!m_hasDefaultAddr) {
        Proxy::Log::error("[UdpOutputChannel] No default destination configured for send()");
        return false;
    }

    if (m_socketFd < 0) {
        openSocket();
        if (m_socketFd < 0) {
            return false;
        }
    }

    ssize_t sent = ::sendto(
        m_socketFd,
        reinterpret_cast<const char*>(message.data()),
        message.size(),
        0,
        reinterpret_cast<const sockaddr*>(&m_defaultAddr),
        sizeof(m_defaultAddr));

    if (sent < 0) {
        Proxy::Log::error("[UdpOutputChannel] sendto failed: {}", std::strerror(errno));
        return false;
    }

    return true;
}

std::size_t UdpOutputChannel::sendBatch(std::span<const std::span<const std::byte>> messages)
{
    std::size_t sentCount = 0;
    for (const auto& msg : messages) {
        if (send(msg)) {
            ++sentCount;
        }
    }
    return sentCount;
}

bool UdpOutputChannel::sendTo(std::span<const std::byte> message, std::uint32_t ipNetworkOrder, std::uint16_t portNetworkOrder)
{
    if (m_socketFd < 0) {
        openSocket();
        if (m_socketFd < 0) {
            return false;
        }
    }

    sockaddr_in destAddr { };
    destAddr.sin_family = AF_INET;
    destAddr.sin_addr.s_addr = ipNetworkOrder;
    destAddr.sin_port = portNetworkOrder;

    ssize_t sent = ::sendto(
        m_socketFd,
        reinterpret_cast<const char*>(message.data()),
        message.size(),
        0,
        reinterpret_cast<const sockaddr*>(&destAddr),
        sizeof(destAddr));

    if (sent < 0) {
        Proxy::Log::error("[UdpOutputChannel] sendTo failed: {}", std::strerror(errno));
        return false;
    }

    return true;
}

bool UdpOutputChannel::sendTo(std::span<const std::byte> message, const std::string& host, std::uint16_t port)
{
    sockaddr_in destAddr { };
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &destAddr.sin_addr) <= 0) {
        Proxy::Log::error("[UdpOutputChannel] Invalid target IP: {}", host);
        return false;
    }

    return sendTo(message, destAddr.sin_addr.s_addr, destAddr.sin_port);
}

int UdpOutputChannel::getNativeSocket() const noexcept
{
    return m_socketFd;
}

} // namespace Core::Net
