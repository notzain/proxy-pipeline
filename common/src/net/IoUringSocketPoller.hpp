#if defined(__linux__)

#pragma once

#include "common/net/ISocketPoller.hpp"

namespace Common::Net {

/**
 * @brief io_uring-based high-performance asynchronous poller for Linux.
 */
class IoUringSocketPoller : public ISocketPoller {
public:
    IoUringSocketPoller();
    ~IoUringSocketPoller() override;

    BytesResult receive(
        int socketFd,
        Common::Compat::span<std::byte> destination,
        Timeout timeout,
        SocketAddress* outSourceAddress = nullptr) override;
    BatchResult receiveBatch(
        int socketFd,
        Common::Compat::span<PollerBufferSlot> slots,
        Timeout timeout,
        Common::Compat::span<SocketAddress> outSourceAddresses = { }) override;

private:
    struct RingImpl;
    RingImpl* m_impl { nullptr };
};

} // namespace Common::Net

#endif // __linux__
