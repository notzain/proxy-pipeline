#pragma once

namespace Core::Handler {
class IHandler;
} // namespace Core::Handler

namespace Core::Threading {

/**
 * @brief Strategy interface for executing the IHandler message polling and event loop.
 */
class IExecutionStrategy {
public:
    virtual ~IExecutionStrategy() = default;

    /**
     * @brief Starts the execution strategy for the given handler.
     *
     * @param handler The handler to run.
     */
    virtual void start(Handler::IHandler& handler) = 0;

    /**
     * @brief Signals the strategy to stop execution.
     */
    virtual void stop() = 0;

    /**
     * @brief Blocks until the execution strategy has finished running.
     */
    virtual void wait() = 0;

    /**
     * @brief Checks if the strategy is currently running.
     *
     * @return true If currently running, false otherwise.
     */
    [[nodiscard]] virtual bool isRunning() const noexcept = 0;
};

} // namespace Core::Threading
