#pragma once

#include "backend/BufferSlot.hpp"
#include "backend/IInputChannel.hpp"
#include "backend/SocketAddress.hpp"
#include "common/compat/Span.hpp"
#include <memory>

namespace Backend {

/**
 * @brief Abstract interface for platform-specific asynchronous/readiness socket and file descriptor polling drivers.
 */
class ISocketPoller {
public:
    using Timeout = IInputChannel::Timeout;

    virtual ~ISocketPoller() = default;

    /**
     * @brief Waits for readability and receives a datagram into destination buffer.
     *
     * @param socketFd The bound socket file descriptor (UDP, UDS, etc.).
     * @param destination Target buffer.
     * @param timeout Maximum wait duration.
     * @param outSourceAddress Optional pointer to record sender's address.
     * @return Typed result distinguishing received data, timeout, closure, and errors.
     */
    virtual BytesResult receive(
        int socketFd,
        Common::Compat::span<std::byte> destination,
        Timeout timeout,
        SocketAddress* outSourceAddress = nullptr) = 0;

    /**
     * @brief Waits for readability and receives a batch of datagrams into destination buffer slots.
     *
     * Uses recvmmsg on Linux or speculative drain on macOS/POSIX.
     *
     * @param socketFd The bound socket file descriptor.
     * @param slots Target buffer slots to populate. Each slot contains a target buffer and tracks received byte count.
     * @param timeout Maximum wait duration for the first packet.
     * @param outSourceAddresses Optional span to record sender addresses.
     * @return Typed result containing the packet count or the receive status.
     */
    virtual BatchResult receiveBatch(
        int socketFd,
        Common::Compat::span<BufferSlot> slots,
        Timeout timeout,
        Common::Compat::span<SocketAddress> outSourceAddresses = { });
};

/**
 * @brief Factory function creating the best available platform socket poller.
 *
 * - On macOS/BSD: Returns KqueueSocketPoller
 * - On Linux: Returns IoUringSocketPoller (or PosixSocketPoller fallback)
 * - On other platforms: Returns PosixSocketPoller
 */
[[nodiscard]] std::unique_ptr<ISocketPoller> createSocketPoller();

} // namespace Backend
