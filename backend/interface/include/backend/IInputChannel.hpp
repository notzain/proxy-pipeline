#pragma once

#include "backend/BufferSlot.hpp"
#include "backend/ReceiveResult.hpp"
#include "common/compat/Span.hpp"
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

namespace Backend {

/**
 * @brief Abstract interface representing a message reception channel.
 *
 * An input channel provides message ingestion from an underlying transport endpoint
 * (e.g. watching a specific directory or listening on a specific UDP port).
 * Built-in execution strategies poll each registered input channel from a single thread.
 * Callers that invoke receive methods from other threads must provide their own synchronization.
 */
class IInputChannel {
public:
    using Timeout = std::chrono::milliseconds;

    explicit IInputChannel(std::string name = "");
    IInputChannel(const IInputChannel&) = default;
    IInputChannel& operator=(const IInputChannel&) = default;
    virtual ~IInputChannel() = default;

    /**
     * @brief Gets the logical identifier or configuration name of this channel.
     */
    [[nodiscard]] std::string_view name() const noexcept;

    /**
     * @brief Receives one message into a buffer slot, attaching an ownership lease if the transport requires settlement.
     *
     * Core zero-copy reception primitive implemented by transport channels. On success `slot.size` holds the
     * received byte count. Transports owning their buffers leave `slot.lease` empty; transports whose messages
     * are owned by an external API attach a Lease that the caller must settle or move onward.
     *
     * @param slot Target slot; `slot.buffer` must be pre-allocated.
     * @param timeout Maximum duration to wait for data.
     * @return A typed result distinguishing data, timeout, closure, and channel errors.
     */
    virtual BytesResult receiveSlot(BufferSlot& slot, Timeout timeout) = 0;

    /**
     * @brief Drains a batch of incoming messages into pre-allocated destination buffer slots.
     *
     * Enables vectorized multi-datagram draining (`recvmmsg` on Linux, speculative non-blocking drain on POSIX/macOS).
     *
     * @param slots Target buffer slots to populate. Each slot contains a target buffer and tracks received byte count.
     * @param timeout Timeout for the initial message wait if no data is currently ready.
     * @return A typed result containing the number of datagrams or the reason no data was received.
     */
    virtual BatchResult receiveBatch(
        Common::Compat::span<BufferSlot> slots,
        Timeout timeout);

protected:
    IInputChannel(IInputChannel&&) noexcept = default;
    IInputChannel& operator=(IInputChannel&&) noexcept = default;

private:
    std::string m_name;
};

} // namespace Backend
