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

    std::size_t receive(int socketFd, std::span<std::byte> destination, Timeout timeout) override;

private:
    struct RingImpl;
    RingImpl* m_impl { nullptr };
};

} // namespace Backend

#endif // __linux__

