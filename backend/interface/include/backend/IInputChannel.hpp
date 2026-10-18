#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
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

    virtual ~IInputChannel() = default;

    /**
     * @brief Polls the input channel for the next available message.
     *
     * @param timeout Maximum duration to wait before returning std::nullopt.
     * @return std::optional<Buffer> Buffer containing received data, or std::nullopt on timeout.
     */
    virtual std::optional<Buffer> receive(Timeout timeout) = 0;

    /**
     * @brief Polls the input channel and reads data directly into a provided buffer without heap allocation.
     *
     * Default implementation falls back to receive(timeout) and copies into destination.
     *
     * @param destination Non-owning pre-allocated memory span to receive data into.
     * @param timeout Maximum duration to wait before returning 0.
     * @return std::size_t Number of bytes written into destination, or 0 on timeout/no message.
     */
    virtual std::size_t receiveInto(std::span<std::byte> destination, Timeout timeout);
};

} // namespace Backend
