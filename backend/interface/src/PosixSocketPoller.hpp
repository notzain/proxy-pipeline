#pragma once

#include "backend/ISocketPoller.hpp"

namespace Backend {

/**
 * @brief Standard POSIX poll() based socket poller implementation.
 */
class PosixSocketPoller : public ISocketPoller {
public:
    PosixSocketPoller() = default;
    ~PosixSocketPoller() override = default;

    std::size_t receive(int socketFd, std::span<std::byte> destination, Timeout timeout) override;
};

} // namespace Backend
