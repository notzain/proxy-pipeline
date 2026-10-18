#pragma once

#include "backend/IOutputChannel.hpp"
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Core {

/**
 * @brief Base class / interface for components that produce/send messages to output channels.
 */
class IMessageSender {
public:
    virtual ~IMessageSender();

    /**
     * @brief Registers an output channel under a specific channel name.
     *
     * @param channelName Name identifying the output channel.
     * @param channel Unique pointer to the output channel.
     */
    void registerOutputChannel(std::string_view channelName, std::unique_ptr<::Backend::IOutputChannel> channel);

    /**
     * @brief Sends a message to a registered output channel.
     *
     * @param channelName Name of the channel to send the message to.
     * @param message View of the byte buffer to send.
     * @return true If the channel was found and message sent successfully, false otherwise.
     */
    bool send(std::string_view channelName, std::span<const std::byte> message);

    /**
     * @brief Sends a message to all registered output channels.
     *
     * @param message View of the byte buffer to send.
     * @return std::size_t Number of output channels the message was successfully dispatched to.
     */
    std::size_t sendAll(std::span<const std::byte> message);

    /**
     * @brief Checks if an output channel is registered with the given name.
     *
     * @param channelName Name of the channel.
     * @return true If registered, false otherwise.
     */
    [[nodiscard]] bool hasOutputChannel(std::string_view channelName) const noexcept;

    /**
     * @brief Returns a list of all registered output channel names.
     *
     * @return std::vector<std::string> Registered channel names.
     */
    [[nodiscard]] std::vector<std::string> getOutputChannelNames() const;

private:
    std::unordered_map<std::string, std::unique_ptr<::Backend::IOutputChannel>> m_outputChannels;
};

} // namespace Core
