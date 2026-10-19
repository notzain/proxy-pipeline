#pragma once

#if defined(__linux__)

#include "backend/socket/ISocketPoller.hpp"

namespace Proxy::Backend::Socket {

/**
 * @brief High-performance Linux io_uring asynchronous UDP socket poller/receiver.
 *
 * Utilizes submission and completion rings (io_uring) to await and reap socket datagrams.
 */
class IoUringSocketPoller : public ISocketPoller {
public:
    IoUringSocketPoller();
    ~IoUringSocketPoller() override;

    IoUringSocketPoller(const IoUringSocketPoller&) = delete;
    IoUringSocketPoller& operator=(const IoUringSocketPoller&) = delete;

    std::size_t receive(int socketFd, std::span<std::byte> destination, Timeout timeout) override;

private:
    struct RingImpl;
    RingImpl* m_impl { nullptr };
};

} // namespace Proxy::Backend::Socket

#endif // __linux__

