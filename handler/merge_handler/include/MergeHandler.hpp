#pragma once

#include "core/handler/IHandler.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace Proxy::Handler {

/**
 * @brief Merge / Fan-in handler that consumes messages from multiple input channels
 * and redirects each incoming message to a single output channel.
 */
class MergeHandler : public Core::Handler::IHandler {
public:
    using Core::Handler::IHandler::IHandler;
    ~MergeHandler() override = default;

    /**
     * @brief Configures MergeHandler and creates input channels and the output channel via the backend.
     *
     * @param config Configuration store.
     * @return true If channel creation succeeded, false otherwise.
     */
    bool onConfigure(const Core::Config::Configuration& config) override;

    /**
     * @brief Callback invoked when a message arrives on any registered input channel.
     * Forwards the message buffer to the target output channel.
     *
     * @param channelName Name of the input channel that received the message.
     * @param message Enveloped message view.
     */
    Core::Message::MessageResult onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message) override;

    /**
     * @brief Callback invoked when a batch of messages arrives on any registered input channel.
     * Forwards the messages in the batch to the target output channel via vectorized sendBatch().
     *
     * @param channelName Name of the input channel that received the messages.
     * @param messages Non-owning view of received enveloped messages.
     */
    void onBatchReceived(
        std::string_view channelName,
        std::span<const Core::Message::MessageView> messages,
        std::span<Core::Message::MessageResult> outResults) override;

private:
    ::Backend::IOutputChannel* m_outputChannel { nullptr };
    std::vector<::Backend::IInputChannel*> m_inputChannelsList;
};

} // namespace Proxy::Handler
