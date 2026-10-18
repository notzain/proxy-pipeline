#include "SplitHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/Configuration.hpp"
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

bool SplitHandler::configure(const Core::Configuration& config, ::Backend::IBackend& backend)
{
    m_inputChannelName = config.getOr<std::string>("split.input_channel", "pipe_in");
    auto inputChannel = backend.createInputChannel(m_inputChannelName, config);
    if (!inputChannel) {
        Proxy::Log::error("[SplitHandler] Failed to create input channel '{}'", m_inputChannelName);
        return false;
    }
    m_inputChannel = registerInputChannel(m_inputChannelName, std::move(inputChannel));

    std::string outChannelsStr = config.getOr<std::string>("split.output_channels", "");
    std::vector<std::string> outChannelNames = splitString(outChannelsStr, ',');

    for (const auto& outName : outChannelNames) {
        auto outChannel = backend.createOutputChannel(outName, config);
        if (!outChannel) {
            Proxy::Log::error("[SplitHandler] Failed to create output channel '{}'", outName);
            return false;
        }
        auto* ptr = registerOutputChannel(outName, std::move(outChannel));
        m_outputChannelsList.push_back(ptr);
    }

    return true;
}

void SplitHandler::onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer)
{
    Proxy::Log::info("[SplitHandler] Channel '{}' received message ({} bytes), splitting to output channels...", channelName, buffer.size());
    std::size_t forwarded = 0;
    if (!m_outputChannelsList.empty()) {
        for (auto* outChan : m_outputChannelsList) {
            if (outChan && outChan->send(buffer)) {
                forwarded++;
            }
        }
    } else {
        forwarded = sendAll(buffer);
    }
    Proxy::Log::debug("[SplitHandler] Successfully forwarded message to {} output channels", forwarded);
}

} // namespace Proxy::Handler
