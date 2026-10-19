#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

#pragma once

#include "backend/uds/IUdsPoller.hpp"

namespace Proxy::Backend::Uds {

/**
 * @brief kqueue-based event driver for BSD and macOS systems for UDS.
 */
class KqueueUdsPoller : public IUdsPoller {
public:
    KqueueUdsPoller();
    ~KqueueUdsPoller() override;

    std::size_t receive(int socketFd, std::span<std::byte> destination, ::Backend::IInputChannel::Timeout timeout) override;

private:
    int m_kqueueFd { -1 };
    int m_registeredSocketFd { -1 };

    void registerSocket(int socketFd);
};

} // namespace Proxy::Backend::Uds

#endif // __APPLE__ || BSD
