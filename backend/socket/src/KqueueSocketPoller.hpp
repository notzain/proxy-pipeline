#pragma once

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

#include "backend/socket/ISocketPoller.hpp"

namespace Proxy::Backend::Socket {

/**
 * @brief High-performance BSD/macOS kqueue(2) socket poller.
 *
 * Utilizes EVFILT_READ to monitor socket readability without linear pollfd scans.
 */
class KqueueSocketPoller : public ISocketPoller {
public:
    KqueueSocketPoller();
    ~KqueueSocketPoller() override;

    KqueueSocketPoller(const KqueueSocketPoller&) = delete;
    KqueueSocketPoller& operator=(const KqueueSocketPoller&) = delete;

    std::size_t receive(int socketFd, std::span<std::byte> destination, Timeout timeout) override;

private:
    int m_kqueueFd { -1 };
    int m_registeredSocketFd { -1 };

    void registerSocket(int socketFd);
};

} // namespace Proxy::Backend::Socket

#endif // __APPLE__ || BSD
