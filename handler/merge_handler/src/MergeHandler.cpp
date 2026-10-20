#include "MergeHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "core/message/MessageView.hpp"
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

MergeHandler::MergeHandler(std::string outputChannelName)
    : m_outputChannelName(std::move(outputChannelName))
{
}

bool MergeHandler::configure(const Core::Config::Configuration& config, ::Backend::IBackend& backend)
{
    m_outputChannelName = config.getOr<std::string>("merge.output_channel", "pipe_out");
    auto outputChannel = backend.createOutputChannel(m_outputChannelName, config);
    if (!outputChannel) {
        Proxy::Log::error("[MergeHandler] Failed to create output channel '{}'", m_outputChannelName);
        return false;
    }
    m_outputChannel = registerOutputChannel(m_outputChannelName, std::move(outputChannel));

    std::string inChannelsStr = config.getOr<std::string>("merge.input_channels", "");
    std::vector<std::string> inChannelNames = splitString(inChannelsStr, ',');

    for (const auto& inName : inChannelNames) {
        auto inChannel = backend.createInputChannel(inName, config);
        if (!inChannel) {
            Proxy::Log::error("[MergeHandler] Failed to create input channel '{}'", inName);
            return false;
        }
        auto* ptr = registerInputChannel(inName, std::move(inChannel));
        m_inputChannelsList.push_back(ptr);
    }

    return true;
}

void MergeHandler::onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message)
{
    Proxy::Log::info("[MergeHandler] Input channel '{}' received message ({} bytes payload), redirecting to '{}'...",
        channelName, message.payload().size(), m_outputChannelName);

    if (m_outputChannel) {
        if (!m_outputChannel->send(message.rawBuffer())) {
            Proxy::Log::warn("[MergeHandler] Failed to redirect message to output channel '{}'", m_outputChannelName);
        }
    } else if (!send(m_outputChannelName, message)) {
        Proxy::Log::warn("[MergeHandler] Failed to redirect message to output channel '{}'", m_outputChannelName);
    }
}

} // namespace Proxy::Handler
