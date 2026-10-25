#pragma once

#include "core/handler/IHandler.hpp"
#include <string>

namespace Proxy::Handler {

/**
 * @brief Test handler that registers an input channel and prints received messages.
 */
class DummyReceiveHandler : public Core::Handler::IHandler {
public:
    using Core::Handler::IHandler::IHandler;
    ~DummyReceiveHandler() override = default;

    /**
     * @brief Configures DummyReceiveHandler and creates the input channel via the backend.
     *
     * @param config Configuration store.
     * @return true If input channel creation succeeded, false otherwise.
     */
    bool onConfigure(const Core::Config::Configuration& config) override;

    /**
     * @brief Callback invoked when a message arrives on an input channel.
     *
     * @param channelName Name of the channel.
     * @param buffer Non-owning view of the raw byte buffer received.
     */
    Core::Message::MessageResult onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message) override;

private:
    std::string m_channelName;
    ::Backend::IInputChannel* m_inputChannel { nullptr };
};

} // namespace Proxy::Handler
