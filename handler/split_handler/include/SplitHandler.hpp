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
    SplitHandler() = default;
    ~SplitHandler() override = default;

    /**
     * @brief Configures SplitHandler and creates the input channel and all output channels via the backend.
     *
     * @param config Configuration store.
     * @param backend Backend instance.
     * @return true If channel creation succeeded, false otherwise.
     */
    bool configure(const Core::Config::Configuration& config, ::Backend::IBackend& backend) override;

    /**
     * @brief Callback invoked when a message is received on an input channel.
     * Forwards the received message buffer to all registered output channels.
     *
     * @param channelName Identifying name of the receiving channel.
     * @param message Enveloped message view.
     */
    void onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message) override;

private:
    std::string m_inputChannelName;
    ::Backend::IInputChannel* m_inputChannel { nullptr };
    std::vector<::Backend::IOutputChannel*> m_outputChannelsList;
};

} // namespace Proxy::Handler
