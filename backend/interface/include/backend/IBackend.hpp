#pragma once

#include "backend/IConfig.hpp"
#include "backend/IInputChannel.hpp"
#include "backend/IOutputChannel.hpp"
#include <memory>
#include <string_view>

namespace Backend {

/**
 * @brief Interface representing a message transport backend.
 *
 * Backends are responsible for configuring transport settings via an abstract
 * IConfig store and creating typed input and output channels.
 */
class IBackend {
public:
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
     * @brief Creates an input channel for message ingestion.
     *
     * @param name Name or identifier of the channel (used to resolve config keys).
     * @param config Strongly-typed key-value configuration store.
     * @return std::unique_ptr<IInputChannel> Created input channel, or nullptr on failure.
     */
    virtual std::unique_ptr<IInputChannel> createInputChannel(std::string_view name, const IConfig& config) = 0;

    /**
     * @brief Creates an output channel for message transmission.
     *
     * @param name Name or identifier of the channel (used to resolve config keys).
     * @param config Strongly-typed key-value configuration store.
     * @return std::unique_ptr<IOutputChannel> Created output channel, or nullptr on failure.
     */
    virtual std::unique_ptr<IOutputChannel> createOutputChannel(std::string_view name, const IConfig& config) = 0;
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
