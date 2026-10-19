#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

#pragma once

#include "backend/ISocketPoller.hpp"

namespace Backend {

/**
 * @brief kqueue-based event driver for BSD and macOS systems.
 */
class KqueueSocketPoller : public ISocketPoller {
public:
    KqueueSocketPoller();
    ~KqueueSocketPoller() override;

    std::size_t receive(int socketFd, std::span<std::byte> destination, Timeout timeout) override;

private:
    int m_kqueueFd { -1 };
    int m_registeredSocketFd { -1 };

    void registerSocket(int socketFd);
};

} // namespace Backend

#endif // __APPLE__ || BSD

