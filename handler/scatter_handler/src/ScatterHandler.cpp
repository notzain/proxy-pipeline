#include "ScatterHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/message/InspectionHeader.hpp"
#include "backend/message/InspectionView.hpp"
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

void ScatterHandler::addOutputChannel(Common::Memory::ObserverPtr<Backend::IOutputChannel> channel)
{
    if (channel) {
        if (!m_outputChannelsStr.empty()) {
            m_outputChannelsStr += ",";
        }
        m_outputChannelsStr += channel->name();
        m_outputChannelsList.push_back(channel);
    }
}

bool ScatterHandler::onConfigure(const Core::Config::Configuration& config)
{
    const auto inputChannelName = config.getOr<std::string>("scatter.input_channel", "scatter_in");
    auto optInput = tryBindInputChannel(inputChannelName, config);
    if (!optInput) {
        return false;
    }

    m_outputChannelsStr = config.getOr<std::string>("scatter.output_channels", "");
    std::vector<std::string> outChannelNames = splitString(m_outputChannelsStr, ',');
    if (outChannelNames.empty()) {
        PROXY_LOG_ERROR("No output channels specified in 'scatter.output_channels'");
        return false;
    }

    for (const auto& outName : outChannelNames) {
        auto optOut = tryBindOutputChannel(outName, config);
        if (!optOut) {
            return false;
        }
        m_outputChannelsList.push_back(optOut.value());
    }

    PROXY_LOG_INFO("Configured with {} inspection branches", m_outputChannelsList.size());
    return true;
}

Backend::MessageResult ScatterHandler::onMessageReceived(
    std::string_view channelName,
    const Backend::MessageView& message)
{
    if (m_outputChannelsList.empty()) {
        PROXY_LOG_WARN("No output channels configured; dropping message");
        return Backend::Dropped { .reason = "No output channels configured", .errorCode = 500 };
    }

    const auto correlationId = m_nextCorrelationId.fetch_add(1, std::memory_order_relaxed);
    const auto totalSplits = static_cast<std::uint16_t>(m_outputChannelsList.size());

    PROXY_LOG_DEBUG("Channel '{}' received message ({} bytes, protocol={}), scattering correlationId={} to {} branches...",
        channelName, message.payload().size(), static_cast<unsigned>(message.protocol().value()), correlationId, totalSplits);

    for (std::uint16_t i = 0; i < totalSplits; ++i) {
        Backend::InspectionHeader header {
            .magic = Backend::PipelineInspectionMagic,
            .correlationId = correlationId,
            .totalSplits = totalSplits,
            .splitIndex = i,
            .verdict = Backend::InspectionVerdict::Pending,
            .errorCode = 0,
            .originalMetadataLength = static_cast<std::uint16_t>(message.metadata().size())
        };

        const auto packedEnvelope = Backend::InspectionView::pack(message, header);
        if (!packedEnvelope) {
            PROXY_LOG_ERROR("Could not serialize inspection envelope for branch {}", i);
            return Backend::Dropped { .reason = "Inspection envelope exceeds envelope size limit", .errorCode = 413 };
        }

        auto optView = Backend::MessageView::from(Common::Compat::span<const std::byte>(packedEnvelope->data(), packedEnvelope->size()));
        if (!optView) {
            PROXY_LOG_ERROR("Failed to serialize inspection envelope for branch {}", i);
            continue;
        }

        auto outChan = m_outputChannelsList[i];
        if (outChan && !outChan->send(*optView)) {
            PROXY_LOG_WARN("Failed to send split {} to branch '{}'", i, outChan->name());
        }
    }

    return Backend::Forwarded { .channel = m_outputChannelsStr };
}

} // namespace Proxy::Handler
