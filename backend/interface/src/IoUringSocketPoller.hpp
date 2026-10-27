#if defined(__linux__)

#pragma once

#include "backend/ISocketPoller.hpp"

namespace Backend {

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
        Common::Compat::span<BufferSlot> slots,
        Timeout timeout,
        Common::Compat::span<SocketAddress> outSourceAddresses = { }) override;

private:
    struct RingImpl;
    RingImpl* m_impl { nullptr };
};

} // namespace Backend

#endif // __linux__
