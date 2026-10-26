#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
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
 */
class IInputChannel {
public:
    using Buffer = std::vector<std::byte>;
    using Timeout = std::chrono::milliseconds;

    explicit IInputChannel(std::string name = "");
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
     * @param timeout Maximum duration to wait before returning 0.
     * @return std::size_t Number of bytes written into destination, or 0 on timeout/no message.
     */
    virtual std::size_t receiveInto(std::span<std::byte> destination, Timeout timeout) = 0;

    /**
     * @brief Polls the input channel for the next available message, returning an allocated buffer.
     *
     * Non-virtual convenience wrapper that delegates to receiveInto().
     *
     * @param timeout Maximum duration to wait before returning std::nullopt.
     * @return std::optional<Buffer> Buffer containing received data, or std::nullopt on timeout.
     */
    std::optional<Buffer> receive(Timeout timeout);

    /**
     * @brief Drains a batch of incoming messages into pre-allocated destination buffer slices.
     *
     * Enables vectorized multi-datagram draining (`recvmmsg` on Linux, speculative non-blocking drain on POSIX/macOS).
     *
     * @param destinations Spans of pre-allocated target buffers.
     * @param outSizes Span of size_t where the received byte counts are stored. Must be at least destinations.size().
     * @param timeout Timeout for the initial message wait if no data is currently ready.
     * @return std::size_t Number of datagrams read into destinations (0 to destinations.size()).
     */
    virtual std::size_t receiveBatch(
        std::span<const std::span<std::byte>> destinations,
        std::span<std::size_t> outSizes,
        Timeout timeout);

private:
    std::string m_name;
};

} // namespace Backend
