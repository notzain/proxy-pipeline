#pragma once

#include "core/IHandler.hpp"
#include <string>
#include <string_view>

namespace Proxy::Handler {

/**
 * @brief Merge / Fan-in handler that consumes messages from multiple input channels
 * and redirects each incoming message to a single output channel.
 */
class MergeHandler : public Core::IHandler {
public:
    MergeHandler() = default;
    explicit MergeHandler(std::string outputChannelName);
    ~MergeHandler() override = default;

    /**
     * @brief Configures MergeHandler and creates input channels and the output channel via the backend.
     *
     * @param config Configuration store.
     * @param backend Backend instance.
     * @return true If channel creation succeeded, false otherwise.
     */
    bool configure(const Core::Configuration& config, ::Backend::IBackend& backend) override;

    /**
     * @brief Callback invoked when a message arrives on any registered input channel.
     * Forwards the message buffer to the target output channel.
     *
     * @param channelName Name of the input channel that received the message.
     * @param buffer Non-owning view of the raw byte buffer received.
     */
    void onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer) override;

private:
    std::string m_outputChannelName;
    ::Backend::IOutputChannel* m_outputChannel { nullptr };
    std::vector<::Backend::IInputChannel*> m_inputChannelsList;
};

} // namespace Proxy::Handler
