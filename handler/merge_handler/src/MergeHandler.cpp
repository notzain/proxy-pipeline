#include "MergeHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/MessageView.hpp"
#include "common/memory/SmallSpanBuffer.hpp"
#include "core/config/Configuration.hpp"
#include "log/Log.hpp"
#include <sstream>
#include <utility>
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

bool MergeHandler::onConfigure(const Core::Config::Configuration& config)
{
    std::string outChannelName = config.getOr<std::string>("merge.output_channel", "pipe_out");
    auto outputChannel = backend().createOutputChannel(outChannelName, config);
    if (!outputChannel) {
        Proxy::Log::error("[MergeHandler] Failed to create output channel '{}'", outChannelName);
        return false;
    }
    m_outputChannel = registerOutputChannel(std::move(outputChannel));

    std::string inChannelsStr = config.getOr<std::string>("merge.input_channels", "");
    std::vector<std::string> inChannelNames = splitString(inChannelsStr, ',');

    for (const auto& inName : inChannelNames) {
        auto inChannel = backend().createInputChannel(inName, config);
        if (!inChannel) {
            Proxy::Log::error("[MergeHandler] Failed to create input channel '{}'", inName);
            return false;
        }
        auto ptr = registerInputChannel(std::move(inChannel));
        m_inputChannelsList.push_back(ptr);
    }

    return true;
}

Backend::MessageResult MergeHandler::onMessageReceived(std::string_view channelName, const Backend::MessageView& message)
{
    std::string_view outName = m_outputChannel ? m_outputChannel->name() : "";
    Proxy::Log::info("[MergeHandler] Input channel '{}' received message ({} bytes payload), redirecting to '{}'...",
        channelName, message.payload().size(), outName);

    if (m_outputChannel) {
        if (!m_outputChannel->send(message.rawBuffer())) {
            Proxy::Log::warn("[MergeHandler] Failed to redirect message to output channel '{}'", outName);
        }
    } else {
        Proxy::Log::warn("[MergeHandler] Output channel '{}' not found", outName);
    }
    return Backend::Forwarded { .channel = outName };
}

void MergeHandler::onBatchReceived(
    std::string_view channelName,
    Common::Compat::span<const Backend::MessageView> messages,
    Common::Compat::span<Backend::MessageResult> outResults)
{
    std::string_view outName = m_outputChannel ? m_outputChannel->name() : "";
    const auto count = std::min(messages.size(), outResults.size());
    for (std::size_t i = 0; i < count; ++i) {
        outResults[i] = Backend::Forwarded { .channel = outName };
    }
    if (messages.empty())
        return;

    Common::Memory::SmallSpanBuffer spans(messages.size());
    for (std::size_t i = 0; i < messages.size(); ++i) {
        spans[i] = messages[i].rawBuffer();
    }

    Proxy::Log::debug("[MergeHandler] Input channel '{}' received batch of {} messages, redirecting to '{}'...",
        channelName, messages.size(), outName);

    if (m_outputChannel) {
        std::size_t sent = m_outputChannel->sendBatch(spans.span(messages.size()));
        if (sent < messages.size()) {
            Proxy::Log::warn("[MergeHandler] Only {}/{} messages redirected to output channel '{}'",
                sent, messages.size(), outName);
        }
    } else {
        Proxy::Log::warn("[MergeHandler] Output channel '{}' not found for sendBatch", outName);
    }
}

} // namespace Proxy::Handler
