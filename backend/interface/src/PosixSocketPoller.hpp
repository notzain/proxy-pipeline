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

    std::size_t receive(
        int socketFd,
        std::span<std::byte> destination,
        Timeout timeout,
        SocketAddress* outSourceAddress = nullptr) override;
    std::size_t receiveBatch(
        int socketFd,
        std::span<const std::span<std::byte>> destinations,
        std::span<std::size_t> outSizes,
        Timeout timeout,
        std::span<SocketAddress> outSourceAddresses = { }) override;
};

} // namespace Backend
