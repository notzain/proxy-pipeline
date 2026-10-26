#pragma once

#include "backend/BufferSlot.hpp"
#include "backend/IInputChannel.hpp"
#include "backend/SocketAddress.hpp"
#include <chrono>
#include <cstddef>
#include <memory>
#include <span>

namespace Backend {

/**
 * @brief Abstract interface for platform-specific asynchronous/readiness socket and file descriptor polling drivers.
 */
class ISocketPoller {
public:
    using Timeout = IInputChannel::Timeout;

    virtual ~ISocketPoller() = default;

    /**
     * @brief Waits for readability and receives a datagram into destination buffer.
     *
     * @param socketFd The bound socket file descriptor (UDP, UDS, etc.).
     * @param destination Target buffer.
     * @param timeout Maximum wait duration.
     * @param outSourceAddress Optional pointer to record sender's address.
     * @return std::size_t Number of bytes received (0 on timeout or error).
     */
    virtual std::size_t receive(
        int socketFd,
        std::span<std::byte> destination,
        Timeout timeout,
        SocketAddress* outSourceAddress = nullptr) = 0;

    /**
     * @brief Waits for readability and receives a batch of datagrams into destination buffer slots.
     *
     * Uses recvmmsg on Linux or speculative drain on macOS/POSIX.
     *
     * @param socketFd The bound socket file descriptor.
     * @param slots Target buffer slots to populate. Each slot contains a target buffer and tracks received byte count.
     * @param timeout Maximum wait duration for the first packet.
     * @param outSourceAddresses Optional span to record sender addresses.
     * @return std::size_t Number of packets received.
     */
    virtual std::size_t receiveBatch(
        int socketFd,
        std::span<BufferSlot> slots,
        Timeout timeout,
        std::span<SocketAddress> outSourceAddresses = { });
};

/**
 * @brief Factory function creating the best available platform socket poller.
 *
 * - On macOS/BSD: Returns KqueueSocketPoller
 * - On Linux: Returns IoUringSocketPoller (or PosixSocketPoller fallback)
 * - On other platforms: Returns PosixSocketPoller
 */
[[nodiscard]] std::unique_ptr<ISocketPoller> createSocketPoller();

} // namespace Backend
