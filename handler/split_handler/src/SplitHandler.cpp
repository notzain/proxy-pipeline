#include "SplitHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/message/MessageView.hpp"
#include "core/config/Configuration.hpp"
#include "log/Log.hpp"
#include <sstream>
#include <string>
#include <vector>

namespace {
std::vector<std::string> splitString(std::string_view str, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream { std::string(str) };
    while (std::getline(tokenStream, token, delimiter)) {
        auto start = token.find_first_not_of(" \t\r\n");
        if (start != std::string::npos) {
            auto end = token.find_last_not_of(" \t\r\n");
            tokens.push_back(token.substr(start, end - start + 1));
        }
    }
    return tokens;
}
} // namespace

namespace Proxy::Handler {

bool SplitHandler::onConfigure(const Core::Config::Configuration& config)
{
    const auto inputChannelName = config.getOr<std::string>("split.input_channel", "pipe_in");
    auto inputChannel = backend().createInputChannel(inputChannelName, config);
    if (!inputChannel) {
        PROXY_LOG_ERROR("Failed to create input channel '{}'", inputChannelName);
        return false;
    }
    registerInputChannel(std::move(inputChannel));

    m_outputChannelsStr = config.getOr<std::string>("split.output_channels", "");
    std::vector<std::string> outChannelNames = splitString(m_outputChannelsStr, ',');

    for (const auto& outName : outChannelNames) {
        auto outChannel = backend().createOutputChannel(outName, config);
        if (!outChannel) {
            PROXY_LOG_ERROR("Failed to create output channel '{}'", outName);
            return false;
        }
        auto ptr = registerOutputChannel(std::move(outChannel));
        m_outputChannelsList.push_back(ptr);
    }

    return true;
}

Backend::MessageResult SplitHandler::onMessageReceived(std::string_view channelName, const Backend::MessageView& message)
{
    PROXY_LOG_INFO("Channel '{}' received message ({} bytes payload), splitting to output channels...",
        channelName, message.payload().size());
    std::size_t forwarded = 0;
    for (auto outChan : m_outputChannelsList) {
        if (outChan && outChan->send(message)) {
            forwarded++;
        }
    }
    PROXY_LOG_DEBUG("Successfully forwarded message to {} output channels", forwarded);
    return Backend::Forwarded { .channel = m_outputChannelsStr };
}

void SplitHandler::onBatchReceived(
    std::string_view channelName,
    Common::Compat::span<const Backend::MessageView> messages,
    Common::Compat::span<Backend::MessageResult> outResults)
{
    const auto count = std::min(messages.size(), outResults.size());
    for (std::size_t i = 0; i < count; ++i) {
        outResults[i] = Backend::Forwarded { .channel = m_outputChannelsStr };
    }
    if (messages.empty())
        return;

    PROXY_LOG_DEBUG("Channel '{}' received batch of {} messages, splitting...", channelName, messages.size());

    for (auto outChan : m_outputChannelsList) {
        if (outChan) {
            outChan->sendBatch(messages);
        }
    }
}

} // namespace Proxy::Handler
