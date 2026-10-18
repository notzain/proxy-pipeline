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

std::size_t IMessageSender::sendAll(std::span<const std::byte> message)
{
    std::size_t count = 0;
    for (const auto& [name, channel] : m_outputChannels) {
        if (channel && channel->send(message)) {
            count++;
        } else {
            Proxy::Log::warn("[IMessageSender] Failed to forward message to channel '{}'", name);
        }
    }
    return count;
}

bool IMessageSender::hasOutputChannel(std::string_view channelName) const noexcept
{
    return m_outputChannels.find(std::string(channelName)) != m_outputChannels.end();
}

std::vector<std::string> IMessageSender::getOutputChannelNames() const
{
    std::vector<std::string> names;
    names.reserve(m_outputChannels.size());
    for (const auto& [name, _] : m_outputChannels) {
        names.push_back(name);
    }
    return names;
}

} // namespace Core
