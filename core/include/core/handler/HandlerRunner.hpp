#pragma once

#include "core/config/Configuration.hpp"
#include "core/handler/ExecutionModel.hpp"
#include "core/handler/IHandler.hpp"
#include "core/telemetry/StatsDExporter.hpp"
#include "core/threading/IExecutionStrategy.hpp"
#include <memory>

namespace Core::Handler {

/**
 * @brief Manages the execution lifecycle, concurrency strategy, POSIX signal handling,
 * and telemetry exporter for an IHandler instance.
 */
class HandlerRunner {
public:
    /**
     * @brief Constructs a HandlerRunner bound to a target handler.
     *
     * @param handler The message handler to orchestrate.
     */
    explicit HandlerRunner(IHandler& handler) noexcept;

    /**
     * @brief Destructor. Stops and waits for active strategies and unregisters signals.
     */
    ~HandlerRunner();

    HandlerRunner(const HandlerRunner&) = delete;
    HandlerRunner& operator=(const HandlerRunner&) = delete;
    HandlerRunner(HandlerRunner&&) = delete;
    HandlerRunner& operator=(HandlerRunner&&) = delete;

    /**
     * @brief Configures the handler and initializes telemetry from the given configuration.
     *
     * @param config Strongly-typed configuration store.
     * @return true If handler configuration succeeded, false otherwise.
     */
    bool configure(const Config::Configuration& config);

    /**
     * @brief Configures live Telemetry StatsD exporter from the configuration.
     *
     * @param config Configuration containing telemetry.statsd.* / telemetry.* settings.
     */
    void configureTelemetry(const Config::Configuration& config);

    /**
     * @brief Sets the concurrency execution model using the ExecutionModel variant.
     *
     * @param model Variant representing SequentialModel, DedicatedThreadModel, or ThreadPoolModel.
     */
    void setExecutionModel(const ExecutionModel& model);

    /**
     * @brief Injects a custom execution strategy directly.
     *
     * @param strategy Custom implementation of IExecutionStrategy.
     */
    void setExecutionStrategy(std::unique_ptr<Threading::IExecutionStrategy> strategy);

    /**
     * @brief Starts the message polling loop synchronously.
     *
     * Configures default strategy and signal handlers, blocks until stop() is invoked or SIGINT/SIGTERM is received.
     */
    void start();

    /**
     * @brief Starts the message polling loop asynchronously without blocking the caller.
     */
    void startAsync();

    /**
     * @brief Blocks until the execution strategy finishes running.
     */
    void wait();

    /**
     * @brief Signals the execution strategy to terminate.
     */
    void stop();

    /**
     * @brief Checks whether the runner event loop is currently active.
     */
    [[nodiscard]] bool isRunning() const noexcept;

private:
    IHandler& m_handler;
    std::unique_ptr<Threading::IExecutionStrategy> m_strategy;
    std::unique_ptr<Telemetry::StatsDExporter> m_telemetryExporter;

    void registerSignalHandler();
    void unregisterSignalHandler();
    void ensureStrategy();
};

} // namespace Core::Handler
