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
     * @brief Prints the received message buffer to standard output using fmt.
     *
     * @param channelName Name of the channel that received the message.
     * @param buffer Non-owning view of the raw byte buffer received.
     */
    void onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer) override;
};

} // namespace Proxy::Handler
