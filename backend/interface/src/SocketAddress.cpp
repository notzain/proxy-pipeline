#include "backend/SocketAddress.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>

namespace Backend {

SocketAddress::SocketAddress() noexcept
    : m_family(AddressFamily::Unknown)
    , m_portNet(0)
    , m_ipv4Net(0)
{
    std::memset(m_ipv6, 0, sizeof(m_ipv6));
}

AddressFamily SocketAddress::family() const noexcept
{
    return m_family;
}

std::uint16_t SocketAddress::port() const noexcept
{
    return ntohs(m_portNet);
}

std::uint16_t SocketAddress::portNet() const noexcept
{
    return m_portNet;
}

std::uint32_t SocketAddress::ipv4Net() const noexcept
{
    return m_ipv4Net;
}

std::string SocketAddress::host() const
{
    if (m_family == AddressFamily::Ipv4) {
        char buf[INET_ADDRSTRLEN] { };
        in_addr in { };
        in.s_addr = m_ipv4Net;
        if (inet_ntop(AF_INET, &in, buf, sizeof(buf))) {
            return std::string(buf);
        }
    } else if (m_family == AddressFamily::Ipv6) {
        char buf[INET6_ADDRSTRLEN] { };
        if (inet_ntop(AF_INET6, m_ipv6, buf, sizeof(buf))) {
            return std::string(buf);
        }
    }
    return "0.0.0.0";
}

std::string SocketAddress::toString() const
{
    if (m_family == AddressFamily::Ipv6) {
        return "[" + host() + "]:" + std::to_string(port());
    }
    return host() + ":" + std::to_string(port());
}

SocketAddress SocketAddress::fromIpv4Net(std::uint32_t ipNetOrder, std::uint16_t portNetOrder) noexcept
{
    SocketAddress addr;
    addr.m_family = AddressFamily::Ipv4;
    addr.m_ipv4Net = ipNetOrder;
    addr.m_portNet = portNetOrder;
    return addr;
}

SocketAddress SocketAddress::fromString(std::string_view hostStr, std::uint16_t portHost)
{
    SocketAddress addr;
    std::string nullTerminated(hostStr);

    in_addr in4 { };
    if (inet_pton(AF_INET, nullTerminated.c_str(), &in4) == 1) {
        addr.m_family = AddressFamily::Ipv4;
        addr.m_ipv4Net = in4.s_addr;
        addr.m_portNet = htons(portHost);
        return addr;
    }

    in6_addr in6 { };
    if (inet_pton(AF_INET6, nullTerminated.c_str(), &in6) == 1) {
        addr.m_family = AddressFamily::Ipv6;
        std::memcpy(addr.m_ipv6, &in6, sizeof(in6));
        addr.m_portNet = htons(portHost);
        return addr;
    }

    return addr;
}

void SocketAddress::setFromNative(const void* sockaddrPtr, std::size_t sockaddrLen) noexcept
{
    if (!sockaddrPtr || sockaddrLen < sizeof(sa_family_t)) {
        m_family = AddressFamily::Unknown;
        return;
    }

    const auto* sa = static_cast<const sockaddr*>(sockaddrPtr);
    if (sa->sa_family == AF_INET && sockaddrLen >= sizeof(sockaddr_in)) {
        const auto* sin = static_cast<const sockaddr_in*>(sockaddrPtr);
        m_family = AddressFamily::Ipv4;
        m_ipv4Net = sin->sin_addr.s_addr;
        m_portNet = sin->sin_port;
    } else if (sa->sa_family == AF_INET6 && sockaddrLen >= sizeof(sockaddr_in6)) {
        const auto* sin6 = static_cast<const sockaddr_in6*>(sockaddrPtr);
        m_family = AddressFamily::Ipv6;
        std::memcpy(m_ipv6, &sin6->sin6_addr, sizeof(sin6->sin6_addr));
        m_portNet = sin6->sin6_port;
    } else {
        m_family = AddressFamily::Unknown;
    }
}

std::size_t SocketAddress::toNative(void* sockaddrPtr, std::size_t maxLen) const noexcept
{
    if (!sockaddrPtr) {
        return 0;
    }

    if (m_family == AddressFamily::Ipv4) {
        if (maxLen < sizeof(sockaddr_in)) {
            return 0;
        }
        auto* sin = static_cast<sockaddr_in*>(sockaddrPtr);
        std::memset(sin, 0, sizeof(*sin));
#ifdef __APPLE__
        sin->sin_len = sizeof(sockaddr_in);
#endif
        sin->sin_family = AF_INET;
        sin->sin_port = m_portNet;
        sin->sin_addr.s_addr = m_ipv4Net;
        return sizeof(sockaddr_in);
    }

    if (m_family == AddressFamily::Ipv6) {
        if (maxLen < sizeof(sockaddr_in6)) {
            return 0;
        }
        auto* sin6 = static_cast<sockaddr_in6*>(sockaddrPtr);
        std::memset(sin6, 0, sizeof(*sin6));
#ifdef __APPLE__
        sin6->sin6_len = sizeof(sockaddr_in6);
#endif
        sin6->sin6_family = AF_INET6;
        sin6->sin6_port = m_portNet;
        std::memcpy(&sin6->sin6_addr, m_ipv6, sizeof(m_ipv6));
        return sizeof(sockaddr_in6);
    }

    return 0;
}

} // namespace Backend
