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

    BytesResult receive(
        int socketFd,
        std::span<std::byte> destination,
        Timeout timeout,
        SocketAddress* outSourceAddress = nullptr) override;
    BatchResult receiveBatch(
        int socketFd,
        std::span<BufferSlot> slots,
        Timeout timeout,
        std::span<SocketAddress> outSourceAddresses = { }) override;
};

} // namespace Backend
