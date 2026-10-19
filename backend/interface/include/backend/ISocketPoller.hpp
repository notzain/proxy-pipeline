#pragma once

#include "backend/IInputChannel.hpp"
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
     * @return std::size_t Number of bytes received (0 on timeout or error).
     */
    virtual std::size_t receive(int socketFd, std::span<std::byte> destination, Timeout timeout) = 0;

    /**
     * @brief Waits for readability and receives a batch of datagrams into destination buffers.
     *
     * Uses recvmmsg on Linux or speculative drain on macOS/POSIX.
     *
     * @param socketFd The bound socket file descriptor.
     * @param destinations Spans of pre-allocated target buffers.
     * @param outSizes Span of size_t where received byte counts are stored.
     * @param timeout Maximum wait duration for the first packet.
     * @return std::size_t Number of packets received.
     */
    virtual std::size_t receiveBatch(
        int socketFd,
        std::span<const std::span<std::byte>> destinations,
        std::span<std::size_t> outSizes,
        Timeout timeout);
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
