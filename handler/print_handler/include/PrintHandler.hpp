#pragma once

#include "core/handler/IHandler.hpp"
#include <span>
#include <string_view>
#include <vector>

namespace Proxy::Handler {

/**
 * @brief Handler that implements Core::Handler::IHandler,
 * printing any received messages to standard output.
 */
class PrintHandler : public Core::Handler::IHandler {
public:
    using Core::Handler::IHandler::IHandler;
    ~PrintHandler() override = default;

    /**
     * @brief Configures PrintHandler and creates the input channel via the backend.
     *
     * @param config Configuration store.
     * @return true If input channel creation succeeded, false otherwise.
     */
    bool onConfigure(const Core::Config::Configuration& config) override;

    /**
     * @brief Prints the received enveloped message payload to standard output using fmt.
     *
     * @param channelName Name of the channel that received the message.
     * @param message Non-owning view of the received enveloped message.
     */
    Core::Message::MessageResult onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message) override;

    /**
     * @brief Batch printing hook for incoming message batches.
     *
     * @param channelName Name of the channel that received the messages.
     * @param messages Non-owning view of received enveloped messages.
     */
    void onBatchReceived(
        std::string_view channelName,
        std::span<const Core::Message::MessageView> messages,
        std::span<Core::Message::MessageResult> outResults) override;
};

} // namespace Proxy::Handler
