#include "DummySendHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/MessageEnvelope.hpp"
#include "common/memory/ByteView.hpp"
#include "core/config/Configuration.hpp"
#include "log/Log.hpp"
#include <utility>

namespace Proxy::Handler {

bool DummySendHandler::onConfigure(const Core::Config::Configuration& config)
{
    const auto channelName = config.getOr<std::string>("dummy.channel", "test_channel");
    m_messageText = config.getOr<std::string>("dummy.message", "Hello from DummySend!");
    int delayMs = config.getOr<int>("dummy.delay_ms", 1000);
    m_delay = std::chrono::milliseconds(delayMs);
    m_lastSendTime = std::chrono::steady_clock::now() - m_delay;

    auto outChannel = backend().createOutputChannel(channelName, config);
    if (!outChannel) {
        Proxy::Log::error("[DummySend] Failed to create output channel '{}'", channelName);
        return false;
    }

    m_outputChannel = registerOutputChannel(std::move(outChannel));
    return true;
}

void DummySendHandler::step()
{
    auto now = std::chrono::steady_clock::now();
    if (now - m_lastSendTime >= m_delay) {
        m_lastSendTime = now;

        auto envelope = Backend::MessageEnvelope::packRaw(Common::Memory::ByteView(m_messageText));

        if (m_outputChannel) {
            Proxy::Log::info("[DummySend] Sending message to channel '{}': {}", m_outputChannel->name(), m_messageText);
            m_outputChannel->send(envelope);
        } else {
            Proxy::Log::warn("[DummySend] No output channel is available");
        }
    }
}

} // namespace Proxy::Handler
