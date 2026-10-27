#pragma once

#include "backend/IInputChannel.hpp"
#include "backend/IOutputChannel.hpp"
#include "core/config/Configuration.hpp"
#include "core/memory/Borrow.hpp"
#include "core/memory/ObserverPtr.hpp"
#include <chrono>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/handler/HandlerTraits.hpp"
#include "core/message/MessageResult.hpp"

namespace Backend {
class IConfig;
class IBackend;
} // namespace Backend

namespace Core::Message {
class MessageView;
} // namespace Core::Message

namespace Core::Handler {

/**
 * @brief Universal base handler managing input and output channels and message dispatch.
 *
 * IHandler provides:
 * - Configuration & Channel Binding: configure() creates and binds backend channels.
 * - Ingress: Registers IInputChannel instances, dispatches received messages via onMessageReceived().
 * - Egress: Registration of IOutputChannel instances returning non-owning pointers for direct sending.
 * - Background work: Periodic step() hook.
 *
 * Sequential execution invokes handler callbacks and step() serially. Dedicated-thread and
 * thread-pool execution may invoke callbacks concurrently and run step() concurrently with
 * callbacks. Handlers using those modes must synchronize shared state unless
 * execution.serialize_handler_access is enabled.
 */
class IHandler {
public:
    using Timeout = std::chrono::milliseconds;

    /**
     * @brief Constructs a handler with its transport backend.
     *
     * @param backend Backend used to create channels and report message results.
     */
    explicit IHandler(::Backend::IBackend& backend) noexcept;

    /**
     * @brief Virtual destructor for safe polymorphic deletion.
     */
    virtual ~IHandler();

    /**
     * @brief Configures the handler from a Configuration store and initializes channels.
     *
     * Delegates to the protected onConfigure() hook for subclass-specific setup.
     *
     * @param config Strongly-typed key-value configuration.
     * @return true If configuration and channel initialization succeeded, false otherwise.
     */
    bool configure(const Config::Configuration& config);

    // --- Input Channel Management (Ingress) ---

    /**
     * @brief Registers an input channel to be polled by the handler event loop.
     *
     * @param channel The input channel instance (which owns its name).
     * @return Core::Memory::Borrow<::Backend::IInputChannel> Rebindable, non-null borrow of the registered channel.
     */
    Core::Memory::Borrow<::Backend::IInputChannel> registerInputChannel(std::unique_ptr<::Backend::IInputChannel> channel);

    /**
     * @brief Visits registered input channels without exposing their ownership storage.
     *
     * @param visitor Callable receiving each input channel.
     */
    template <typename Visitor>
        requires std::invocable<Visitor&, ::Backend::IInputChannel&>
    void forEachInputChannel(Visitor&& visitor) const
    {
        for (const auto& inputChannel : m_inputChannels) {
            std::invoke(visitor, *inputChannel);
        }
    }

    /**
     * @brief Gets the number of registered input channels.
     */
    [[nodiscard]] std::size_t inputChannelCount() const noexcept;

    /**
     * @brief Ingress callback invoked when an enveloped message has been received on an input channel.
     *
     * Subclasses interested in processing incoming messages override this.
     * Default implementation returns Consumed.
     *
     * @param channelName Name of the channel that received the message.
     * @param message Validated non-owning view of the envelope, metadata, and payload.
     * @return MessageResult Strongly-typed result (Forwarded, Consumed, or Dropped).
     */
    [[nodiscard]] virtual Message::MessageResult onMessageReceived(std::string_view channelName, const Message::MessageView& message);

    /**
     * @brief Ingress callback invoked when a batch of enveloped messages has been received on an input channel.
     *
     * Handlers can override this to perform batched validation and vectorized egress dispatching.
     * Default implementation iterates over onMessageReceived().
     *
     * @param channelName Name of the channel that received the messages.
     * @param messages Non-owning views of the received messages.
     * @param outResults Output span populated with one result per message in input order.
     */
    virtual void onBatchReceived(
        std::string_view channelName,
        std::span<const Message::MessageView> messages,
        std::span<Message::MessageResult> outResults);

    /**
     * @brief Dispatches a received batch of messages to the handler.
     *
     * Invokes the batch callback and reports each returned result alongside its corresponding
     * message to the injected backend.
     *
     * @param channelName Name of the channel that received the messages.
     * @param messages Non-owning views of the received messages.
     */
    void processBatch(std::string_view channelName, std::span<const Message::MessageView> messages);

    // --- Output Channel Management (Egress) ---

    /**
     * @brief Registers an output channel to be managed by the handler.
     *
     * @param channel The output channel instance.
     * @return Core::Memory::Borrow<::Backend::IOutputChannel> Rebindable, non-null borrow of the registered channel.
     */
    Core::Memory::Borrow<::Backend::IOutputChannel> registerOutputChannel(std::unique_ptr<::Backend::IOutputChannel> channel);

    // --- Operational Metadata & Classification ---

    /**
     * @brief Declares the high-level role of this handler in pipeline topology.
     *
     * @return HandlerRole (Processor by default; Generator, ExternalIngress, or ExternalEgress).
     */
    [[nodiscard]] virtual HandlerRole role() const noexcept { return HandlerRole::Processor; }

    /**
     * @brief Declares the external network or application protocol utilized by this handler.
     *
     * @return HandlerProtocol (None by default; Udp, Http, etc.).
     */
    [[nodiscard]] virtual HandlerProtocol protocol() const noexcept { return HandlerProtocol::None; }

    // --- Periodic Processing Hook ---

    /**
     * @brief Periodic step callback executed on each iteration of the event loop.
     *
     * Handlers that need to perform background work, periodic sending, etc., can override this.
     * In parallel execution models this may overlap message callbacks unless handler access
     * serialization is enabled.
     */
    virtual void step();

    [[nodiscard]] ::Backend::IBackend& backend() noexcept;

protected:
    /**
     * @brief Subclass configuration hook invoked by configure().
     *
     * Subclasses override this method to read handler-specific keys, create their input/output
     * channels via the injected backend, and store direct handles to channels.
     * Subclasses do not need to call any base class configure methods.
     *
     * @param config Strongly-typed key-value configuration.
     * @return true If handler-specific configuration succeeded, false otherwise.
     */
    virtual bool onConfigure(const Config::Configuration& /*config*/)
    {
        return true;
    }

private:
    std::vector<std::unique_ptr<::Backend::IInputChannel>> m_inputChannels;
    std::vector<std::unique_ptr<::Backend::IOutputChannel>> m_outputChannels;
    ::Backend::IBackend& m_backend;
};

} // namespace Core::Handler
