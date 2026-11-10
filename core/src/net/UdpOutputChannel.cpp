#include "core/net/UdpOutputChannel.hpp"
#include "log/Log.hpp"
#include <algorithm>
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

int UdpOutputChannel::ensureSocketForFamily(Common::Net::AddressFamily family)
{
    if (family == Common::Net::AddressFamily::Ipv6) {
        if (!m_socketFdV6) {
            m_socketFdV6.reset(::socket(AF_INET6, SOCK_DGRAM, 0));
            if (!m_socketFdV6) {
                PROXY_LOG_ERROR("Failed to create IPv6 UDP socket: {error_message}", std::strerror(errno));
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
            PROXY_LOG_ERROR("Failed to create IPv4 UDP socket: {error_message}", std::strerror(errno));
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
    ensureSocketForFamily(Common::Net::AddressFamily::Ipv4);

    if (!m_defaultHost.empty() && m_defaultPort != 0) {
        m_defaultAddr = Common::Net::SocketAddress::fromString(m_defaultHost, m_defaultPort);
        if (m_defaultAddr.family() != Common::Net::AddressFamily::Unknown) {
            ensureSocketForFamily(m_defaultAddr.family());
            m_hasDefaultAddr = true;
            PROXY_LOG_INFO("Configured default target UDP {target_host}:{target_port}", m_defaultHost, m_defaultPort);
        } else {
            PROXY_LOG_ERROR("Invalid default target IP: {target_host}", m_defaultHost);
        }
    }
}

bool UdpOutputChannel::send(Common::Compat::span<const std::byte> message)
{
    if (!m_hasDefaultAddr) {
        PROXY_LOG_ERROR("No default destination configured for send()");
        return false;
    }

    return sendTo(message, m_defaultAddr);
}

std::size_t UdpOutputChannel::sendBatch(Common::Compat::span<const Common::Compat::span<const std::byte>> messages)
{
    if (messages.empty()) {
        return 0;
    }

#if defined(__linux__)
    if (!m_hasDefaultAddr) {
        PROXY_LOG_ERROR("No default destination configured for sendBatch()");
        return 0;
    }

    int fd = ensureSocketForFamily(m_defaultAddr.family());
    if (fd < 0) {
        return 0;
    }

    sockaddr_storage ss { };
    std::size_t addrLen = m_defaultAddr.toNative(&ss, sizeof(ss));
    if (addrLen == 0) {
        PROXY_LOG_ERROR("Invalid destination SocketAddress in sendBatch()");
        return 0;
    }

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
            msgvec[i].msg_hdr.msg_name = &ss;
            msgvec[i].msg_hdr.msg_namelen = static_cast<socklen_t>(addrLen);
        }

        int sentCount = ::sendmmsg(fd, msgvec, static_cast<unsigned int>(chunk), 0);
        if (sentCount > 0) {
            totalSent += static_cast<std::size_t>(sentCount);
            if (static_cast<std::size_t>(sentCount) < chunk) {
                break;
            }
        } else if (sentCount < 0 && (errno == ENOBUFS || errno == EAGAIN)) {
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
    std::size_t sentCount = 0;
    for (const auto& msg : messages) {
        if (send(msg)) {
            ++sentCount;
        }
    }
    return sentCount;
#endif
}

bool UdpOutputChannel::sendTo(Common::Compat::span<const std::byte> message, const Common::Net::SocketAddress& destination)
{
    int fd = ensureSocketForFamily(destination.family());
    if (fd < 0) {
        return false;
    }

    sockaddr_storage ss { };
    std::size_t addrLen = destination.toNative(&ss, sizeof(ss));
    if (addrLen == 0) {
        PROXY_LOG_ERROR("Invalid destination SocketAddress");
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
        PROXY_LOG_ERROR("sendTo failed: {error_message}", std::strerror(errno));
        return false;
    }

    return true;
}

bool UdpOutputChannel::sendTo(Common::Compat::span<const std::byte> message, std::uint32_t ipNetworkOrder, std::uint16_t portNetworkOrder)
{
    return sendTo(message, Common::Net::SocketAddress::fromIpv4Net(ipNetworkOrder, portNetworkOrder));
}

bool UdpOutputChannel::sendTo(Common::Compat::span<const std::byte> message, const std::string& host, std::uint16_t port)
{
    auto addr = Common::Net::SocketAddress::fromString(host, port);
    if (addr.family() == Common::Net::AddressFamily::Unknown) {
        PROXY_LOG_ERROR("Invalid target IP: {target_host}", host);
        return false;
    }

    return sendTo(message, addr);
}

int UdpOutputChannel::getNativeSocket() const noexcept
{
    return m_socketFd.get();
}

} // namespace Core::Net
