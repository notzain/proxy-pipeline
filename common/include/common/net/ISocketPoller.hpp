#pragma once

#include "common/compat/Span.hpp"
#include "common/net/PollerBufferSlot.hpp"
#include "common/net/PollerResult.hpp"
#include "common/net/SocketAddress.hpp"
#include <chrono>
#include <memory>

namespace Common::Net {

/**
 * @brief Abstract interface for platform-specific asynchronous/readiness socket and file descriptor polling drivers.
 */
class ISocketPoller {
public:
    using Timeout = std::chrono::milliseconds;

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
    virtual PollerBytesResult receive(
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
    virtual PollerBatchResult receiveBatch(
        int socketFd,
        Common::Compat::span<PollerBufferSlot> slots,
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

} // namespace Common::Net
