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
    int m_kqueueFd { -1 };
    int m_registeredSocketFd { -1 };

    bool registerSocket(int socketFd);
};

} // namespace Backend

#endif // __APPLE__ || BSD
