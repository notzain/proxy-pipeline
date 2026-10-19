#pragma once

#include "backend/IInputChannel.hpp"
#include <chrono>
#include <cstddef>
#include <memory>
#include <span>

namespace Proxy::Backend::Socket {

/**
 * @brief Abstract interface for platform-specific asynchronous/readiness socket polling drivers.
 */
class ISocketPoller {
public:
    using Timeout = ::Backend::IInputChannel::Timeout;

    virtual ~ISocketPoller() = default;

    /**
     * @brief Waits for readability and receives a datagram into destination buffer.
     *
     * @param socketFd The bound UDP socket file descriptor.
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

} // namespace Proxy::Backend::Socket

