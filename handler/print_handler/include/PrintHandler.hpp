#pragma once

#include "core/IHandler.hpp"
#include <span>
#include <string_view>

namespace Proxy::Handler {

/**
 * @brief Handler that implements Core::IHandler and Core::IMessageReceiver,
 * printing any received messages to standard output.
 */
class PrintHandler : public Core::IHandler {
public:
    PrintHandler() = default;
    ~PrintHandler() override = default;

    /**
     * @brief Configures PrintHandler and creates the input channel via the backend.
     *
     * @param config Configuration store.
     * @param backend Backend instance.
     * @return true If input channel creation succeeded, false otherwise.
     */
    bool configure(const Core::Configuration& config, ::Backend::IBackend& backend) override;

    /**
     * @brief Prints the received message buffer to standard output using fmt.
     *
     * @param channelName Name of the channel that received the message.
     * @param buffer Non-owning view of the raw byte buffer received.
     */
    void onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer) override;

private:
    std::string m_channelName;
    ::Backend::IInputChannel* m_inputChannel { nullptr };
};

} // namespace Proxy::Handler
