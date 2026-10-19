#if defined(__linux__)

#pragma once

#include "backend/uds/IUdsPoller.hpp"
#include <liburing.h>

namespace Proxy::Backend::Uds {

/**
 * @brief io_uring-based high-performance asynchronous poller for Linux for UDS.
 */
class IoUringUdsPoller : public IUdsPoller {
public:
    IoUringUdsPoller();
    ~IoUringUdsPoller() override;

    std::size_t receive(int socketFd, std::span<std::byte> destination, ::Backend::IInputChannel::Timeout timeout) override;

private:
    struct io_uring m_ring { };
    bool m_initialized { false };
};

} // namespace Proxy::Backend::Uds

#endif // __linux__
