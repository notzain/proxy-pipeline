#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace Core {

/**
 * @brief Interface and base event loop for message handling components.
 *
 * IHandler defines the message lifecycle hooks (reception and retrieval)
 * as well as a managed loop for polling incoming messages until stopped.
 * It also installs signal handling for graceful shutdown on SIGINT (Ctrl+C)
 * and SIGTERM.
 */
class IHandler {
public:
    using Buffer = std::vector<std::byte>;
    using Timeout = std::chrono::milliseconds;

    /**
     * @brief Virtual destructor for safe polymorphic deletion.
     */
    virtual ~IHandler();

    /**
     * @brief Callback invoked when a message has been successfully retrieved.
     *
     * @param buffer Non-owning view of the raw byte buffer received.
     */
    virtual void onMessageReceived(std::span<const std::byte> buffer) = 0;

    /**
     * @brief Retrieves the next available message within the given timeout.
     *
     * In default implementations, this may delegate to a linked backend or source.
     *
     * @param timeout Maximum duration to wait for a message before returning.
     * @return std::optional<Buffer> Containing the buffer if a message was received, or std::nullopt on timeout.
     */
    virtual std::optional<Buffer> nextMessage(Timeout timeout) = 0;

    /**
     * @brief Starts the message polling loop.
     *
     * Runs continuously until stop() is invoked or a shutdown signal (SIGINT/Ctrl+C) is received.
     * Repeatedly polls nextMessage() and dispatches valid messages to onMessageReceived().
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

    void registerSignalHandler();
    void unregisterSignalHandler();
};

} // namespace Core
