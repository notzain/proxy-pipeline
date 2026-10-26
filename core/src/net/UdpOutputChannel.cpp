#include "core/net/UdpOutputChannel.hpp"
#include "log/Log.hpp"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace Core::Net {

UdpOutputChannel::UdpOutputChannel(std::string name, std::string defaultHost, std::uint16_t defaultPort, std::size_t sendBufferSize)
    : ::Backend::IOutputChannel(std::move(name))
    , m_defaultHost(std::move(defaultHost))
    , m_defaultPort(defaultPort)
    , m_sendBufferSize(sendBufferSize)
{
    openSocket();
}

UdpOutputChannel::~UdpOutputChannel() = default;

UdpOutputChannel::UdpOutputChannel(UdpOutputChannel&& other) noexcept
    : ::Backend::IOutputChannel(std::move(other))
    , m_defaultHost(std::move(other.m_defaultHost))
    , m_defaultPort(other.m_defaultPort)
    , m_sendBufferSize(other.m_sendBufferSize)
    , m_socketFd(std::move(other.m_socketFd))
    , m_socketFdV6(std::move(other.m_socketFdV6))
    , m_defaultAddr(other.m_defaultAddr)
    , m_hasDefaultAddr(other.m_hasDefaultAddr)
{
}

UdpOutputChannel& UdpOutputChannel::operator=(UdpOutputChannel&& other) noexcept
{
    if (this != &other) {
        ::Backend::IOutputChannel::operator=(std::move(other));
        m_defaultHost = std::move(other.m_defaultHost);
        m_defaultPort = other.m_defaultPort;
        m_sendBufferSize = other.m_sendBufferSize;
        m_socketFd = std::move(other.m_socketFd);
        m_socketFdV6 = std::move(other.m_socketFdV6);
        m_defaultAddr = other.m_defaultAddr;
        m_hasDefaultAddr = other.m_hasDefaultAddr;
    }
    return *this;
}

void UdpOutputChannel::closeSocket()
{
    m_socketFd.reset();
    m_socketFdV6.reset();
}

int UdpOutputChannel::ensureSocketForFamily(::Backend::AddressFamily family)
{
    if (family == ::Backend::AddressFamily::Ipv6) {
        if (!m_socketFdV6) {
            m_socketFdV6.reset(::socket(AF_INET6, SOCK_DGRAM, 0));
            if (!m_socketFdV6) {
                Proxy::Log::error("[UdpOutputChannel] Failed to create IPv6 UDP socket: {}", std::strerror(errno));
                return -1;
            }
            int sndbuf = static_cast<int>(m_sendBufferSize);
            ::setsockopt(m_socketFdV6.get(), SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
        }
        return m_socketFdV6.get();
    }

    if (!m_socketFd) {
        m_socketFd.reset(::socket(AF_INET, SOCK_DGRAM, 0));
        if (!m_socketFd) {
            Proxy::Log::error("[UdpOutputChannel] Failed to create IPv4 UDP socket: {}", std::strerror(errno));
            return -1;
        }
        int sndbuf = static_cast<int>(m_sendBufferSize);
        ::setsockopt(m_socketFd.get(), SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    }
    return m_socketFd.get();
}

void UdpOutputChannel::openSocket()
{
    closeSocket();
    ensureSocketForFamily(::Backend::AddressFamily::Ipv4);

    if (!m_defaultHost.empty() && m_defaultPort != 0) {
        m_defaultAddr = ::Backend::SocketAddress::fromString(m_defaultHost, m_defaultPort);
        if (m_defaultAddr.family() != ::Backend::AddressFamily::Unknown) {
            ensureSocketForFamily(m_defaultAddr.family());
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

    return sendTo(message, m_defaultAddr);
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

bool UdpOutputChannel::sendTo(std::span<const std::byte> message, const ::Backend::SocketAddress& destination)
{
    int fd = ensureSocketForFamily(destination.family());
    if (fd < 0) {
        return false;
    }

    sockaddr_storage ss { };
    std::size_t addrLen = destination.toNative(&ss, sizeof(ss));
    if (addrLen == 0) {
        Proxy::Log::error("[UdpOutputChannel] Invalid destination SocketAddress");
        return false;
    }

    ssize_t sent = ::sendto(
        fd,
        reinterpret_cast<const char*>(message.data()),
        message.size(),
        0,
        reinterpret_cast<const sockaddr*>(&ss),
        static_cast<socklen_t>(addrLen));

    if (sent < 0) {
        Proxy::Log::error("[UdpOutputChannel] sendTo failed: {}", std::strerror(errno));
        return false;
    }

    return true;
}

bool UdpOutputChannel::sendTo(std::span<const std::byte> message, std::uint32_t ipNetworkOrder, std::uint16_t portNetworkOrder)
{
    return sendTo(message, ::Backend::SocketAddress::fromIpv4Net(ipNetworkOrder, portNetworkOrder));
}

bool UdpOutputChannel::sendTo(std::span<const std::byte> message, const std::string& host, std::uint16_t port)
{
    auto addr = ::Backend::SocketAddress::fromString(host, port);
    if (addr.family() == ::Backend::AddressFamily::Unknown) {
        Proxy::Log::error("[UdpOutputChannel] Invalid target IP: {}", host);
        return false;
    }

    return sendTo(message, addr);
}

int UdpOutputChannel::getNativeSocket() const noexcept
{
    return m_socketFd.get();
}

} // namespace Core::Net
