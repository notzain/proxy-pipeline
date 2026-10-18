#pragma once

#include "backend/IInputChannel.hpp"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Core {

/**
 * @brief Base event loop for message handling components.
 *
 * IHandler defines the message polling and execution loop. It manages
 * registered input channels, dispatches incoming messages to IMessageReceiver callbacks
 * (if implemented), and runs custom per-iteration work via step().
 * It also installs signal handling for graceful shutdown on SIGINT (Ctrl+C) and SIGTERM.
 */
class IHandler {
public:
    using Timeout = std::chrono::milliseconds;

    /**
     * @brief Structure representing a registered input channel with its identifying name.
     */
    struct NamedInputChannel {
        std::string name;
        std::unique_ptr<::Backend::IInputChannel> channel;
    };

    /**
     * @brief Virtual destructor for safe polymorphic deletion.
     */
    virtual ~IHandler();

    /**
     * @brief Registers an input channel to be polled by the handler event loop.
     *
     * @param channelName Name of the channel (used when dispatching to IMessageReceiver).
     * @param channel The input channel instance.
     */
    void registerInputChannel(std::string_view channelName, std::unique_ptr<::Backend::IInputChannel> channel);

    /**
     * @brief Periodic step callback executed on each iteration of the event loop.
     *
     * Handlers that need to perform background work, periodic sending, etc., can override this.
     */
    virtual void step();

    /**
     * @brief Starts the message polling loop.
     *
     * Runs continuously until stop() is invoked or a shutdown signal (SIGINT/Ctrl+C) is received.
     * Repeatedly polls all registered input channels, dispatches to IMessageReceiver, and calls step().
     */
    virtual void start();

    /**
     * @brief Signals the handler loop to terminate.
     */
    virtual void stop();

    /**
     * @brief Checks whether the handler event loop is currently active.
     *
     * @return true If the loop is running.
     * @return false If the loop has stopped or has not started.
     */
    [[nodiscard]] bool isRunning() const noexcept;

protected:
    std::atomic<bool> m_running { false };
    std::vector<NamedInputChannel> m_inputChannels;

    void registerSignalHandler();
    void unregisterSignalHandler();
};

} // namespace Core
