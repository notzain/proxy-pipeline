#pragma once

#include "core/IHandler.hpp"
#include "core/IMessageReceiver.hpp"
#include "core/IMessageSender.hpp"
#include <string_view>

namespace Proxy::Handler {

/**
 * @brief Split / Fan-out handler that consumes messages from an input channel
 * and forwards each received message to all configured output channels.
 */
class SplitHandler : public Core::IHandler, public Core::IMessageReceiver, public Core::IMessageSender {
public:
    SplitHandler() = default;
    ~SplitHandler() override = default;

    /**
     * @brief Callback invoked when a message is received on an input channel.
     * Forwards the received message buffer to all registered output channels.
     *
     * @param channelName Identifying name of the receiving channel.
     * @param buffer Raw message byte buffer.
     */
    void onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer) override;
};

} // namespace Proxy::Handler
