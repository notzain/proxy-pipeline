#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace Backend {

/**
 * @brief Network address family enumeration.
 */
enum class AddressFamily : std::uint8_t {
    Unknown = 0,
    Ipv4 = 1,
    Ipv6 = 2,
    Unix = 3
};

/**
 * @brief High-performance, trivially-copyable network endpoint abstraction.
 *
 * Encapsulates IP/Unix network addresses with zero heap allocations and without
 * exposing raw POSIX/OS socket headers in public interfaces.
 */
class SocketAddress {
public:
    SocketAddress() noexcept;

    /**
     * @brief Gets the network address family.
     */
    [[nodiscard]] AddressFamily family() const noexcept;

    /**
     * @brief Gets the port in host byte order.
     */
    [[nodiscard]] std::uint16_t port() const noexcept;

    /**
     * @brief Gets the port in network byte order.
     */
    [[nodiscard]] std::uint16_t portNet() const noexcept;

    /**
     * @brief Gets the IPv4 address in network byte order.
     */
    [[nodiscard]] std::uint32_t ipv4Net() const noexcept;

    /**
     * @brief Gets a pointer to the 16-byte IPv6 address in network byte order.
     */
    [[nodiscard]] const std::uint8_t* ipv6Bytes() const noexcept;

    /**
     * @brief Gets the host address formatted as a string (e.g. "127.0.0.1").
     */
    [[nodiscard]] std::string host() const;

    /**
     * @brief Gets the full address formatted as "host:port" (e.g. "127.0.0.1:9123").
     */
    [[nodiscard]] std::string toString() const;

    /**
     * @brief Creates a SocketAddress from IPv4 and port in network byte order.
     */
    static SocketAddress fromIpv4Net(std::uint32_t ipNetOrder, std::uint16_t portNetOrder) noexcept;

    /**
     * @brief Creates a SocketAddress from a 16-byte IPv6 address and port in network byte order.
     */
    static SocketAddress fromIpv6Net(const std::uint8_t ipv6NetOrder[16], std::uint16_t portNetOrder) noexcept;

    /**
     * @brief Creates a SocketAddress from a hostname/IP string and host-order port.
     */
    static SocketAddress fromString(std::string_view host, std::uint16_t port);

    /**
     * @brief Populates the address from a native POSIX sockaddr pointer.
     */
    void setFromNative(const void* sockaddrPtr, std::size_t sockaddrLen) noexcept;

    /**
     * @brief Serializes the address into a native POSIX sockaddr buffer.
     * @return Number of bytes written into the native buffer (0 on failure/unsupported).
     */
    std::size_t toNative(void* sockaddrPtr, std::size_t maxLen) const noexcept;

private:
    AddressFamily m_family { AddressFamily::Unknown };
    std::uint16_t m_portNet { 0 };
    std::uint32_t m_ipv4Net { 0 };
    std::uint8_t m_ipv6[16] { 0 };
};

} // namespace Backend
