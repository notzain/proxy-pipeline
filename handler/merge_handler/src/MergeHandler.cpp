#include "MergeHandler.hpp"
#include "log/Log.hpp"
#include <utility>

namespace Proxy::Handler {

MergeHandler::MergeHandler(std::string outputChannelName)
    : m_outputChannelName(std::move(outputChannelName))
{
}

void MergeHandler::onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer)
{
    Proxy::Log::info("[MergeHandler] Input channel '{}' received message ({} bytes), redirecting to '{}'...",
        channelName, buffer.size(), m_outputChannelName);

    if (!send(m_outputChannelName, buffer)) {
        Proxy::Log::warn("[MergeHandler] Failed to redirect message to output channel '{}'", m_outputChannelName);
    }
}

} // namespace Proxy::Handler
