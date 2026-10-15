#pragma once

#include "core/Configuration.hpp"
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace Core {

/**
 * @brief Interface representing a message transport backend.
 *
 * Backends are responsible for receiving/ingesting incoming messages from
 * their respective sources (files, UDP sockets, etc.).
 */
class IBackend {
public:
    using Buffer = std::vector<std::byte>;
    using Timeout = std::chrono::milliseconds;

    /**
     * @brief Virtual destructor for polymorphic cleanup.
     */
    virtual ~IBackend() = default;

    /**
     * @brief Configures the backend instance using a Configuration store.
     *
     * @param config Key-value configuration for the backend.
     */
    virtual void configure(const Configuration& config) = 0;

    /**
     * @brief Polls the underlying transport for the next available message.
     *
     * @param timeout Maximum duration to wait before giving up.
     * @return std::optional<Buffer> Buffer containing received data, or std::nullopt on timeout.
     */
    virtual std::optional<Buffer> nextMessage(Timeout timeout) = 0;
};

/**
 * @brief Factory function implemented by whichever concrete backend library is linked.
 *
 * This allows handlers and executables to instantiate the configured backend
 * purely through link-time resolution without any compile-time #ifdefs or knowledge
 * of concrete backend headers/classes.
 *
 * @return std::unique_ptr<IBackend> The instantiated backend implementation.
 */
std::unique_ptr<IBackend> createBackend();

} // namespace Core
