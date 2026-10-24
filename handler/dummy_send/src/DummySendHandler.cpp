#include "DummySendHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "core/message/MessageEnvelope.hpp"
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

bool DummySendHandler::configure(const Core::Config::Configuration& config, ::Backend::IBackend& backend)
{
    IHandler::configure(config, backend);

    m_channelName = config.getOr<std::string>("dummy.channel", "test_channel");
    m_messageText = config.getOr<std::string>("dummy.message", "Hello from DummySend!");
    int delayMs = config.getOr<int>("dummy.delay_ms", 1000);
    m_delay = std::chrono::milliseconds(delayMs);
    m_lastSendTime = std::chrono::steady_clock::now() - m_delay;

    auto outChannel = backend.createOutputChannel(m_channelName, config);
    if (!outChannel) {
        Proxy::Log::error("[DummySend] Failed to create output channel '{}'", m_channelName);
        return false;
    }

    m_outputChannel = registerOutputChannel(m_channelName, std::move(outChannel));
    return true;
}

void DummySendHandler::step()
{
    auto now = std::chrono::steady_clock::now();
    if (now - m_lastSendTime >= m_delay) {
        m_lastSendTime = now;

        std::span<const std::byte> byteSpan(
            reinterpret_cast<const std::byte*>(m_messageText.data()),
            m_messageText.size());

        auto envelope = Core::Message::MessageEnvelope::packRaw(byteSpan);

        Proxy::Log::info("[DummySend] Sending message to channel '{}': {}", m_channelName, m_messageText);
        if (m_outputChannel) {
            m_outputChannel->send(envelope);
        } else {
            Proxy::Log::warn("[DummySend] Output channel '{}' not found", m_channelName);
        }
    }
}

} // namespace Proxy::Handler
