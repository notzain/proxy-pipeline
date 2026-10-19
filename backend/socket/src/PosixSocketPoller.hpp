#pragma once

#include "backend/socket/ISocketPoller.hpp"

namespace Proxy::Backend::Socket {

/**
 * @brief Portable POSIX poll(2) socket poller fallback.
 */
class PosixSocketPoller : public ISocketPoller {
public:
    PosixSocketPoller() = default;
    ~PosixSocketPoller() override = default;

    std::size_t receive(int socketFd, std::span<std::byte> destination, Timeout timeout) override;
};

} // namespace Proxy::Backend::Socket

