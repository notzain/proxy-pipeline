#include "SplitHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "core/memory/SmallSpanBuffer.hpp"
#include "core/message/MessageView.hpp"
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
    m_inputChannelName = config.getOr<std::string>("split.input_channel", "pipe_in");
    auto inputChannel = backend().createInputChannel(m_inputChannelName, config);
    if (!inputChannel) {
        Proxy::Log::error("[SplitHandler] Failed to create input channel '{}'", m_inputChannelName);
        return false;
    }
    m_inputChannel = registerInputChannel(m_inputChannelName, std::move(inputChannel));

    m_outputChannelsStr = config.getOr<std::string>("split.output_channels", "");
    std::vector<std::string> outChannelNames = splitString(m_outputChannelsStr, ',');

    for (const auto& outName : outChannelNames) {
        auto outChannel = backend().createOutputChannel(outName, config);
        if (!outChannel) {
            Proxy::Log::error("[SplitHandler] Failed to create output channel '{}'", outName);
            return false;
        }
        auto ptr = registerOutputChannel(std::move(outChannel));
        m_outputChannelsList.push_back(ptr);
    }

    return true;
}

Core::Message::MessageResult SplitHandler::onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message)
{
    Proxy::Log::info("[SplitHandler] Channel '{}' received message ({} bytes payload), splitting to output channels...",
        channelName, message.payload().size());
    std::size_t forwarded = 0;
    for (auto outChan : m_outputChannelsList) {
        if (outChan && outChan->send(message.rawBuffer())) {
            forwarded++;
        }
    }
    Proxy::Log::debug("[SplitHandler] Successfully forwarded message to {} output channels", forwarded);
    return Core::Message::Forwarded { .channel = m_outputChannelsStr };
}

void SplitHandler::onBatchReceived(
    std::string_view channelName,
    std::span<const Core::Message::MessageView> messages,
    std::span<Core::Message::MessageResult> outResults)
{
    const auto count = std::min(messages.size(), outResults.size());
    for (std::size_t i = 0; i < count; ++i) {
        outResults[i] = Core::Message::Forwarded { .channel = m_outputChannelsStr };
    }
    if (messages.empty())
        return;

    Core::Memory::SmallSpanBuffer spans(messages.size());
    for (std::size_t i = 0; i < messages.size(); ++i) {
        spans[i] = messages[i].rawBuffer();
    }

    Proxy::Log::debug("[SplitHandler] Channel '{}' received batch of {} messages, splitting...", channelName, messages.size());

    for (auto outChan : m_outputChannelsList) {
        if (outChan) {
            outChan->sendBatch(spans.span(messages.size()));
        }
    }
}

} // namespace Proxy::Handler
