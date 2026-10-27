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
        Common::Compat::span<std::byte> destination,
        Timeout timeout,
        SocketAddress* outSourceAddress = nullptr) override;
    BatchResult receiveBatch(
        int socketFd,
        Common::Compat::span<BufferSlot> slots,
        Timeout timeout,
        Common::Compat::span<SocketAddress> outSourceAddresses = { }) override;
};

} // namespace Backend
