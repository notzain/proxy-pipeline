#pragma once

#include "backend/IInputChannel.hpp"
#include "backend/SocketAddress.hpp"
#include "core/io/UniqueFd.hpp"
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
 * Listens on a specified IP address and UDP port, reading incoming datagrams and packing
 * them directly into binary envelopes containing UdpMetadata (source/dest IP and port)
 * in-place with zero heap allocations using platform-specific socket polling drivers.
 *
 * This channel is decoupled from any specific Backend::IBackend transport.
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
    UdpInputChannel(std::string name, std::string host, std::uint16_t port, std::size_t receiveBufferSize = 8 * 1024 * 1024);
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
     * @brief Reads a datagram and serializes an in-place envelope directly into destination.
     */
    std::size_t receiveInto(std::span<std::byte> destination, Timeout timeout) override;

    /**
     * @brief Drains a batch of incoming UDP datagrams into destination buffers as envelopes.
     */
    std::size_t receiveBatch(
        std::span<const std::span<std::byte>> destinations,
        std::span<std::size_t> outSizes,
        Timeout timeout) override;

    /**
     * @brief Gets the native socket file descriptor.
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
    Core::Io::UniqueFd m_socketFd;
    ::Backend::SocketAddress m_boundAddr;
    std::unique_ptr<::Backend::ISocketPoller> m_poller;
};

} // namespace Core::Net
