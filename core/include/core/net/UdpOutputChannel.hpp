#pragma once

#include "backend/IOutputChannel.hpp"
#include <cstddef>
#include <cstdint>
#include <netinet/in.h>
#include <span>
#include <string>

namespace Core::Net {

/**
 * @brief Standalone UDP message transmission channel implementing Backend::IOutputChannel.
 *
 * Supports sending messages to a default configured destination or dynamically
 * directing individual packets to specific destination IPv4:port addresses
 * (such as reply-to-origin in proxy egress).
 */
class UdpOutputChannel : public ::Backend::IOutputChannel {
public:
    /**
     * @brief Constructs a UDP output channel with an optional default destination.
     *
     * @param defaultHost Default destination IP or hostname (can be empty if sendTo is used).
     * @param defaultPort Default destination UDP port.
     * @param sendBufferSize Socket send buffer size (SO_SNDBUF) in bytes. Defaults to 8MB.
     */
    explicit UdpOutputChannel(
        std::string defaultHost = "",
        std::uint16_t defaultPort = 0,
        std::size_t sendBufferSize = 8 * 1024 * 1024);

    ~UdpOutputChannel() override;

    UdpOutputChannel(const UdpOutputChannel&) = delete;
    UdpOutputChannel& operator=(const UdpOutputChannel&) = delete;
    UdpOutputChannel(UdpOutputChannel&&) noexcept;
    UdpOutputChannel& operator=(UdpOutputChannel&&) noexcept;

    /**
     * @brief Transmits a message to the default destination address.
     */
    bool send(std::span<const std::byte> message) override;

    /**
     * @brief Transmits a batch of messages to the default destination address.
     */
    std::size_t sendBatch(std::span<const std::span<const std::byte>> messages) override;

    /**
     * @brief Sends a datagram directly to a specified destination address in network byte order.
     *
     * @param message Message payload bytes.
     * @param ipNetworkOrder 32-bit IPv4 address in network byte order.
     * @param portNetworkOrder 16-bit UDP port in network byte order.
     * @return true If transmission succeeded, false otherwise.
     */
    bool sendTo(std::span<const std::byte> message, std::uint32_t ipNetworkOrder, std::uint16_t portNetworkOrder);

    /**
     * @brief Sends a datagram directly to a specified destination host and port.
     */
    bool sendTo(std::span<const std::byte> message, const std::string& host, std::uint16_t port);

    /**
     * @brief Gets the native socket file descriptor.
     */
    [[nodiscard]] int getNativeSocket() const noexcept;

private:
    void openSocket();
    void closeSocket();

    std::string m_defaultHost;
    std::uint16_t m_defaultPort { 0 };
    std::size_t m_sendBufferSize { 8 * 1024 * 1024 };
    int m_socketFd { -1 };
    sockaddr_in m_defaultAddr { };
    bool m_hasDefaultAddr { false };
};

} // namespace Core::Net
