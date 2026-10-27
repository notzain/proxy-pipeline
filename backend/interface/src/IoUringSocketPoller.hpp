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
        std::span<std::byte> destination,
        Timeout timeout,
        SocketAddress* outSourceAddress = nullptr) override;
    BatchResult receiveBatch(
        int socketFd,
        std::span<BufferSlot> slots,
        Timeout timeout,
        std::span<SocketAddress> outSourceAddresses = { }) override;

private:
    struct RingImpl;
    RingImpl* m_impl { nullptr };
};

} // namespace Backend

#endif // __linux__
