#pragma once

#include "core/handler/IHandler.hpp"
#include <string_view>

namespace Proxy::Handler {

/**
 * @brief Split / Fan-out handler that consumes messages from an input channel
 * and forwards each received message to all configured output channels.
 */
class SplitHandler : public Core::Handler::IHandler {
public:
    using Core::Handler::IHandler::IHandler;
    ~SplitHandler() override = default;

    /**
     * @brief Configures SplitHandler and creates the input channel and all output channels via the backend.
     *
     * @param config Configuration store.
     * @return true If channel creation succeeded, false otherwise.
     */
    bool onConfigure(const Core::Config::Configuration& config) override;

    /**
     * @brief Callback invoked when a message is received on an input channel.
     * Forwards the received message buffer to all registered output channels.
     *
     * @param channelName Identifying name of the receiving channel.
     * @param message Enveloped message view.
     */
    Backend::MessageResult onMessageReceived(std::string_view channelName, const Backend::MessageView& message) override;

    /**
     * @brief Callback invoked when a batch of messages is received on an input channel.
     * Forwards all messages in the batch using vectorized sendBatch().
     *
     * @param channelName Identifying name of the receiving channel.
     * @param messages Non-owning view of received enveloped messages.
     */
    void onBatchReceived(
        std::string_view channelName,
        Common::Compat::span<const Backend::MessageView> messages,
        Common::Compat::span<Backend::MessageResult> outResults) override;

private:
    std::string m_outputChannelsStr;
    std::vector<Common::Memory::ObserverPtr<::Backend::IOutputChannel>> m_outputChannelsList;
};

} // namespace Proxy::Handler
