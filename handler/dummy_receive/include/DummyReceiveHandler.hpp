#pragma once

#include "core/handler/IHandler.hpp"
#include <string>

namespace Proxy::Handler {

/**
 * @brief Test handler that registers an input channel and prints received messages.
 */
class DummyReceiveHandler : public Core::Handler::IHandler {
public:
    DummyReceiveHandler() = default;
    ~DummyReceiveHandler() override = default;

    /**
     * @brief Configures DummyReceiveHandler and creates the input channel via the backend.
     *
     * @param config Configuration store.
     * @param backend Backend instance.
     * @return true If input channel creation succeeded, false otherwise.
     */
    bool configure(const Core::Config::Configuration& config, ::Backend::IBackend& backend) override;

    /**
     * @brief Callback invoked when a message arrives on an input channel.
     *
     * @param channelName Name of the channel.
     * @param buffer Non-owning view of the raw byte buffer received.
     */
    void onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer) override;

private:
    std::string m_channelName;
    ::Backend::IInputChannel* m_inputChannel { nullptr };
};

} // namespace Proxy::Handler
