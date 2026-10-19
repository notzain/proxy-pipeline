#pragma once

#include "backend/uds/IUdsPoller.hpp"

namespace Proxy::Backend::Uds {

/**
 * @brief Standard POSIX poll() based poller implementation for UDS.
 */
class PosixUdsPoller : public IUdsPoller {
public:
    PosixUdsPoller() = default;
    ~PosixUdsPoller() override = default;

    std::size_t receive(int socketFd, std::span<std::byte> destination, ::Backend::IInputChannel::Timeout timeout) override;
};

} // namespace Proxy::Backend::Uds
