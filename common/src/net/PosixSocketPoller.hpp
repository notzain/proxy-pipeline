#pragma once

#include "common/net/ISocketPoller.hpp"

namespace Common::Net {

/**
 * @brief Standard POSIX poll() based socket poller implementation.
 */
class PosixSocketPoller : public ISocketPoller {
public:
    PosixSocketPoller() = default;
    ~PosixSocketPoller() override = default;

    PollerBytesResult receive(
        int socketFd,
        Common::Compat::span<std::byte> destination,
        Timeout timeout,
        SocketAddress* outSourceAddress = nullptr) override;
    PollerBatchResult receiveBatch(
        int socketFd,
        Common::Compat::span<PollerBufferSlot> slots,
        Timeout timeout,
        Common::Compat::span<SocketAddress> outSourceAddresses = { }) override;
};

} // namespace Common::Net
