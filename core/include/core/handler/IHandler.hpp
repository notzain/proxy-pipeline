#pragma once

#include "backend/IInputChannel.hpp"
#include "backend/IOutputChannel.hpp"
#include "core/config/Configuration.hpp"
#include "core/threading/IExecutionStrategy.hpp"
#include <chrono>
#include <cstddef>
#include <memory>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Backend {
class IConfig;
class IBackend;
} // namespace Backend

namespace Core::Message {
class MessageView;
} // namespace Core::Message

namespace Core::Handler {

/**
 * @brief Concurrency and execution model for the handler event loop.
 */
enum class ExecutionModel {
    Sequential, ///< Single-threaded round-robin polling.
    Dedicated, ///< Dedicated thread per registered input channel.
    ThreadPool ///< Dedicated input polling with worker thread pool for message dispatch.
};

/**
 * @brief Parses an execution model enum from a string name.
 *
 * @param name Model name (e.g. "sequential", "dedicated", "thread_pool").
 * @return ExecutionModel Corresponding enum value, defaulting to Dedicated if unknown.
 */
ExecutionModel parseExecutionModel(std::string_view name);

/**
 * @brief Universal base handler managing input and output channels, message dispatch,
 * and the execution lifecycle loop.
 *
 * IHandler provides:
 * - Configuration & Channel Binding: configure() creates and binds backend channels.
 * - Ingress: Registers IInputChannel instances, dispatches received messages via onMessageReceived().
 * - Egress: Thread-safe registration of IOutputChannel instances, sending via send() and sendAll().
 * - Background work: Periodic step() hook.
 * - Runtime Execution Models: Configurable via setExecutionModel() (Sequential, Dedicated, ThreadPool).
 * - Async lifecycle: start(), startAsync(), wait(), stop(), isRunning().
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
     * @brief Configures the handler from a Configuration store and initializes channels using the provided backend.
     *
     * Subclasses override this method to read handler-specific keys, create their input/output
     * channels via the backend, and store direct handles to channels.
     *
     * @param config Strongly-typed key-value configuration.
     * @param backend Underlying transport backend for creating channels.
     * @return true If configuration and channel initialization succeeded, false otherwise.
     */
    virtual bool configure(const Config::Configuration& config, ::Backend::IBackend& backend);

    // --- Input Channel Management (Ingress) ---

    /**
     * @brief Registers an input channel to be polled by the handler event loop.
     *
     * @param channelName Name of the channel.
     * @param channel The input channel instance.
     * @return ::Backend::IInputChannel* Non-owning pointer to the registered channel.
     */
    ::Backend::IInputChannel* registerInputChannel(std::string_view channelName, std::unique_ptr<::Backend::IInputChannel> channel);

    /**
     * @brief Accessor for registered input channels.
     *
     * @return const std::vector<NamedInputChannel>& Reference to input channels.
     */
    [[nodiscard]] const std::vector<NamedInputChannel>& getInputChannels() const noexcept;

    /**
     * @brief Ingress callback invoked when an enveloped message has been received on an input channel.
     *
     * Subclasses interested in processing incoming messages override this.
     * Default implementation is a no-op.
     *
     * @param channelName Name of the channel that received the message.
     * @param message Validated non-owning view of the envelope, metadata, and payload.
     */
    virtual void onMessageReceived(std::string_view channelName, const Message::MessageView& message);

    /**
     * @brief Ingress callback invoked when a batch of enveloped messages has been received on an input channel.
     *
     * Handlers can override this to perform batched validation and vectorized egress dispatching.
     * Default implementation iterates over onMessageReceived().
     *
     * @param channelName Name of the channel that received the messages.
     * @param messages Non-owning views of the received messages.
     */
    virtual void onBatchReceived(std::string_view channelName, std::span<const Message::MessageView> messages);

    // --- Output Channel Management (Egress) ---

    /**
     * @brief Registers an output channel under a specific channel name in a thread-safe manner.
     *
     * @param channelName Name identifying the output channel.
     * @param channel The output channel instance.
     * @return ::Backend::IOutputChannel* Non-owning pointer to the registered channel.
     */
    ::Backend::IOutputChannel* registerOutputChannel(std::string_view channelName, std::unique_ptr<::Backend::IOutputChannel> channel);

    /**
     * @brief Sends an enveloped message to a registered output channel in a thread-safe manner.
     *
     * @param channelName Name of the channel to send the message to.
     * @param message The message view whose rawBuffer() is dispatched.
     * @return true If the channel was found and message sent successfully, false otherwise.
     */
    bool send(std::string_view channelName, const Message::MessageView& message);

    /**
     * @brief Sends a batch of enveloped messages to a registered output channel in a thread-safe manner.
     *
     * @param channelName Name of the channel to send the messages to.
     * @param messages Views of messages to transmit.
     * @return std::size_t Number of messages successfully dispatched.
     */
    std::size_t sendBatch(std::string_view channelName, std::span<const Message::MessageView> messages);

    /**
     * @brief Sends a message to a registered output channel in a thread-safe manner.
     *
     * @param channelName Name of the channel to send the message to.
     * @param message View of the byte buffer to send.
     * @return true If the channel was found and message sent successfully, false otherwise.
     */
    bool send(std::string_view channelName, std::span<const std::byte> message);

    /**
     * @brief Sends a batch of messages to a registered output channel in a thread-safe manner.
     *
     * Acquires the channel lock once for the entire batch.
     *
     * @param channelName Name of the channel to send the messages to.
     * @param messages View of message byte spans to send.
     * @return std::size_t Number of messages successfully dispatched.
     */
    std::size_t sendBatch(std::string_view channelName, std::span<const std::span<const std::byte>> messages);

    /**
     * @brief Sends a message to all registered output channels in a thread-safe manner.
     *
     * @param message View of the byte buffer to send.
     * @return std::size_t Number of output channels the message was successfully dispatched to.
     */
    std::size_t sendAll(std::span<const std::byte> message);

    /**
     * @brief Checks if an output channel is registered with the given name.
     *
     * @param channelName Name of the channel.
     * @return true If registered, false otherwise.
     */
    [[nodiscard]] bool hasOutputChannel(std::string_view channelName) const noexcept;

    /**
     * @brief Returns a list of all registered output channel names.
     *
     * @return std::vector<std::string> Registered channel names.
     */
    [[nodiscard]] std::vector<std::string> getOutputChannelNames() const;

    // --- Periodic Processing Hook ---

    /**
     * @brief Periodic step callback executed on each iteration of the event loop.
     *
     * Handlers that need to perform background work, periodic sending, etc., can override this.
     */
    virtual void step();

    // --- Execution Strategy & Concurrency Configuration ---

    /**
     * @brief Sets the execution model using standard strategies.
     *
     * @param model Desired execution model (Sequential, Dedicated, or ThreadPool).
     * @param poolSize Worker thread count (only used when model is ThreadPool, 0 for hardware concurrency).
     * @param strictFifo Enforces strict per-flow FIFO order in ThreadPoolStrategy (default: true).
     */
    void setExecutionModel(ExecutionModel model, std::size_t poolSize = 0, bool strictFifo = true);

    /**
     * @brief Injects a custom execution strategy.
     *
     * @param strategy Custom implementation of IExecutionStrategy.
     */
    void setExecutionStrategy(std::unique_ptr<Threading::IExecutionStrategy> strategy);

    // --- Lifecycle Execution Control ---

    /**
     * @brief Starts the message polling loop synchronously.
     *
     * Blocks the calling thread until stop() is invoked or a shutdown signal (SIGINT/SIGTERM) is received.
     */
    virtual void start();

    /**
     * @brief Starts the message polling loop asynchronously without blocking.
     *
     * Launches the configured strategy in background thread(s) and returns immediately.
     */
    virtual void startAsync();

    /**
     * @brief Blocks until the handler and its execution strategy finish running.
     */
    virtual void wait();

    /**
     * @brief Signals the handler loop and its execution strategy to terminate.
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
    /**
     * @brief Transparent hasher enabling C++20 heterogeneous lookup in `m_outputChannels`.
     *
     * By providing `using is_transparent = void;`, `std::unordered_map::find` can look up
     * channels directly using `std::string_view` without allocating temporary `std::string`
     * objects on the heap during hot egress dispatch.
     */
    struct TransparentStringHash {
        using is_transparent = void;

        [[nodiscard]] std::size_t operator()(std::string_view sv) const noexcept
        {
            return std::hash<std::string_view> { }(sv);
        }
    };

    std::vector<NamedInputChannel> m_inputChannels;
    mutable std::shared_mutex m_outputMutex;
    std::unordered_map<std::string, std::unique_ptr<::Backend::IOutputChannel>, TransparentStringHash, std::equal_to<>> m_outputChannels;
    std::unique_ptr<Threading::IExecutionStrategy> m_strategy;

    void registerSignalHandler();
    void unregisterSignalHandler();
    void ensureStrategy();
};

} // namespace Core::Handler
