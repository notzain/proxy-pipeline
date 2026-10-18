#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace Core {

/**
 * @brief Abstract interface for components that consume/receive messages.
 */
class IMessageReceiver {
public:
    virtual ~IMessageReceiver() = default;

    /**
     * @brief Callback invoked when a message has been received on a channel.
     *
     * @param channelName Name of the channel that received the message.
     * @param buffer Non-owning view of the raw byte buffer received.
     */
    virtual void onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer) = 0;
};

} // namespace Core
