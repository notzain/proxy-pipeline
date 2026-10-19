#pragma once

#include "backend/IInputChannel.hpp"
#include <chrono>
#include <cstddef>
#include <span>

namespace Proxy::Backend::Uds {

/**
 * @brief Abstract poller interface for waiting on socket readiness with timeout.
 */
class IUdsPoller {
public:
    virtual ~IUdsPoller() = default;

    /**
     * @brief Waits for read readiness on socketFd and receives message into destination.
     *
     * @param socketFd Bound socket file descriptor.
     * @param destination Destination buffer span to fill.
     * @param timeout Maximum duration to wait.
     * @return std::size_t Number of bytes read, or 0 if timeout / error.
     */
    virtual std::size_t receive(int socketFd, std::span<std::byte> destination, ::Backend::IInputChannel::Timeout timeout) = 0;
};

/**
 * @brief Factory function creating platform-optimal poller implementation.
 */
std::unique_ptr<IUdsPoller> createUdsPoller();

} // namespace Proxy::Backend::Uds
