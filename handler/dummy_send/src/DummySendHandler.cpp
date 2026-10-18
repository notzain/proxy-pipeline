#include "DummySendHandler.hpp"
#include "log/Log.hpp"
#include <utility>

namespace Proxy::Handler {

DummySendHandler::DummySendHandler(std::string channelName, std::string messageText, std::chrono::milliseconds delay)
    : m_channelName(std::move(channelName))
    , m_messageText(std::move(messageText))
    , m_delay(delay)
    , m_lastSendTime(std::chrono::steady_clock::now() - delay)
{
}

void DummySendHandler::step()
{
    auto now = std::chrono::steady_clock::now();
    if (now - m_lastSendTime >= m_delay) {
        m_lastSendTime = now;

        std::span<const std::byte> byteSpan(
            reinterpret_cast<const std::byte*>(m_messageText.data()),
            m_messageText.size());

        Proxy::Log::info("[DummySend] Sending message to channel '{}': {}", m_channelName, m_messageText);
        send(m_channelName, byteSpan);
    }
}

} // namespace Proxy::Handler
