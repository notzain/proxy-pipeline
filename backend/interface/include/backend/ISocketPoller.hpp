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

