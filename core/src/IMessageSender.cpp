#include "core/IMessageSender.hpp"
#include "log/Log.hpp"

namespace Core {

IMessageSender::~IMessageSender() = default;

void IMessageSender::registerOutputChannel(std::string_view channelName, std::unique_ptr<::Backend::IOutputChannel> channel)
{
    if (!channel) {
        Proxy::Log::warn("[IMessageSender] Attempted to register null output channel '{}'", channelName);
        return;
    }
    m_outputChannels[std::string(channelName)] = std::move(channel);
}

bool IMessageSender::send(std::string_view channelName, std::span<const std::byte> message)
{
    auto it = m_outputChannels.find(std::string(channelName));
    if (it == m_outputChannels.end() || !it->second) {
        Proxy::Log::error("[IMessageSender] Output channel '{}' not found", channelName);
        return false;
    }
    return it->second->send(message);
}

bool IMessageSender::hasOutputChannel(std::string_view channelName) const noexcept
{
    return m_outputChannels.find(std::string(channelName)) != m_outputChannels.end();
}

} // namespace Core
