#pragma once

#include "backend/IConfig.hpp"
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace Backend {

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
     * @brief Configures the backend instance using an abstract IConfig store.
     *
     * @param config Strongly-typed key-value configuration.
     */
    virtual void configure(const IConfig& config) = 0;

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
 * purely through link-time resolution without compile-time knowledge of concrete classes.
 *
 * @return std::unique_ptr<IBackend> The instantiated backend implementation.
 */
std::unique_ptr<IBackend> createBackend();

} // namespace Backend
