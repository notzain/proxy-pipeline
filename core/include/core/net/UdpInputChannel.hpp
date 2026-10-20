#pragma once

#include "backend/IInputChannel.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace Backend {
class ISocketPoller;
} // namespace Backend

namespace Core::Net {

/**
 * @brief Standalone UDP message reception channel implementing Backend::IInputChannel.
 *
 * Listens on a specified IP address and UDP port, reading incoming datagrams
 * using the platform's high-performance socket polling driver (kqueue on macOS,
 * io_uring / recvmmsg on Linux).
 *
 * This channel is decoupled from any specific Backend::IBackend transport, allowing
 * it to serve as a network ingress channel inside handlers regardless of what backend
 * is linked for pipeline egress.
 */
class UdpInputChannel : public ::Backend::IInputChannel {
public:
    using Timeout = ::Backend::IInputChannel::Timeout;

    /**
     * @brief Constructs and binds a UDP input channel.
     *
     * @param host IP address or hostname to bind to (e.g. "0.0.0.0" or "127.0.0.1").
     * @param port UDP port to listen on.
     * @param receiveBufferSize Socket receive buffer size (SO_RCVBUF) in bytes. Defaults to 8MB.
     */
    explicit UdpInputChannel(std::string host, std::uint16_t port, std::size_t receiveBufferSize = 8 * 1024 * 1024);

    /**
     * @brief Destructor. Closes the bound socket.
     */
    ~UdpInputChannel() override;

    UdpInputChannel(const UdpInputChannel&) = delete;
    UdpInputChannel& operator=(const UdpInputChannel&) = delete;
    UdpInputChannel(UdpInputChannel&&) noexcept;
    UdpInputChannel& operator=(UdpInputChannel&&) noexcept;

    /**
     * @brief Polls the UDP socket for the next available datagram.
     *
     * @param timeout Maximum duration to wait before timing out.
     * @return std::optional<Buffer> Buffer containing the received datagram, or std::nullopt.
     */
    std::optional<Buffer> receive(Timeout timeout) override;

    /**
     * @brief Reads a datagram directly into a pre-allocated memory buffer without heap allocation.
     *
     * @param destination Non-owning memory span to receive data into.
     * @param timeout Maximum duration to wait.
     * @return std::size_t Number of bytes received (0 on timeout or error).
     */
    std::size_t receiveInto(std::span<std::byte> destination, Timeout timeout) override;

    /**
     * @brief Drains a batch of incoming UDP datagrams in a single polling cycle.
     *
     * @param destinations Spans of pre-allocated target buffers.
     * @param outSizes Span of size_t where received byte counts are stored.
     * @param timeout Maximum duration to wait for the first datagram.
     * @return std::size_t Number of datagrams read.
     */
    std::size_t receiveBatch(
        std::span<const std::span<std::byte>> destinations,
        std::span<std::size_t> outSizes,
        Timeout timeout) override;

    /**
     * @brief Gets the native socket file descriptor.
     *
     * @return int Native socket FD, or -1 if closed.
     */
    [[nodiscard]] int getNativeSocket() const noexcept;

    /**
     * @brief Gets the bound host address.
     */
    [[nodiscard]] const std::string& getHost() const noexcept;

    /**
     * @brief Gets the bound UDP port.
     */
    [[nodiscard]] std::uint16_t getPort() const noexcept;

private:
    void openAndBindSocket();
    void closeSocket();

    std::string m_host;
    std::uint16_t m_port { 0 };
    std::size_t m_receiveBufferSize { 8 * 1024 * 1024 };
    int m_socketFd { -1 };
    std::unique_ptr<::Backend::ISocketPoller> m_poller;
};

} // namespace Core::Net
