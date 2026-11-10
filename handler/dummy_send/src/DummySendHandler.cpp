#include "DummySendHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/message/EnvelopeHeader.hpp"
#include "backend/message/MessageView.hpp"
#include "common/memory/ByteView.hpp"
#include "core/config/Configuration.hpp"
#include "log/Log.hpp"
#include <utility>

namespace Proxy::Handler {

bool DummySendHandler::onConfigure(const Core::Config::Configuration& config)
{
    const auto channelName = config.getOr<std::string>("dummy.channel", "test_channel");
    m_messageText = config.getOr<std::string>("dummy.message", "Hello from DummySend!");
    if (m_messageText.size() > Backend::MaxEnvelopeFrameBytes - sizeof(Backend::EnvelopeHeader)) {
        PROXY_LOG_ERROR("Configured message exceeds the maximum envelope payload size");
        return false;
    }

    int delayMs = config.getOr<int>("dummy.delay_ms", 1000);
    m_delay = std::chrono::milliseconds(delayMs);
    m_lastSendTime = std::chrono::steady_clock::now() - m_delay;

    auto optOut = tryBindOutputChannel(channelName, config);
    if (!optOut) {
        return false;
    }
    m_outputChannel = optOut.value();
    return true;
}

void DummySendHandler::step()
{
    auto now = std::chrono::steady_clock::now();
    if (now - m_lastSendTime >= m_delay) {
        m_lastSendTime = now;

        const auto envelope = Backend::MessageView::packRaw(Common::Memory::ByteView(m_messageText));
        if (!envelope) {
            PROXY_LOG_ERROR("Configured message could not be packed within the envelope size limit");
            return;
        }

        if (m_outputChannel) {
            PROXY_LOG_INFO("Sending message to channel '{}': {}", m_outputChannel->name(), m_messageText);
            m_outputChannel->send(*envelope);
        } else {
            PROXY_LOG_WARN("No output channel is available");
        }
    }
}

} // namespace Proxy::Handler
