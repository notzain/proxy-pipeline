#pragma once

#include "backend/BufferSlot.hpp"
#include "backend/ReceiveResult.hpp"
#include <chrono>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
    using Buffer = std::vector<std::byte>;
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
     * @brief Polls the input channel and reads data directly into a provided buffer without heap allocation.
     *
     * Core zero-copy reception primitive implemented by transport channels.
     *
     * @param destination Non-owning pre-allocated memory span to receive data into.
     * @param timeout Maximum duration to wait for data.
     * @return A typed result distinguishing data, timeout, closure, and channel errors.
     */
    virtual BytesResult receiveInto(std::span<std::byte> destination, Timeout timeout) = 0;

    /**
     * @brief Polls the input channel for the next available message, returning an allocated buffer.
     *
     * Non-virtual convenience wrapper that delegates to receiveInto().
     *
     * @param timeout Maximum duration to wait for data.
     * @return A typed result containing the received buffer or the reason no data was received.
     */
    ReceiveMessageResult receive(Timeout timeout);

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
        std::span<BufferSlot> slots,
        Timeout timeout);

protected:
    IInputChannel(IInputChannel&&) noexcept = default;
    IInputChannel& operator=(IInputChannel&&) noexcept = default;

private:
    std::string m_name;
};

} // namespace Backend
