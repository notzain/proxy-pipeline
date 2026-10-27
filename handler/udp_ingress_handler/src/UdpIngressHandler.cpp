#include "UdpIngressHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/IOutputChannel.hpp"
#include "common/memory/SmallSpanBuffer.hpp"
#include "core/config/Configuration.hpp"
#include "core/message/MessageView.hpp"
#include "core/net/UdpInputChannel.hpp"
#include "log/Log.hpp"
#include <array>
#include <utility>

namespace Proxy::Handler {

bool UdpIngressHandler::onConfigure(const Core::Config::Configuration& config)
{
    const auto outputChannelName = config.getOr<std::string>("ingress.output_channel", "ingress_out");

    // 1. Output channel is created through the active backend (e.g. SHM, UDS, etc.)
    auto outChannel = backend().createOutputChannel(outputChannelName, config);
    if (!outChannel) {
        Proxy::Log::error("[UdpIngressHandler] Failed to create pipeline output channel '{}'", outputChannelName);
        return false;
    }
    m_outputChannel = registerOutputChannel(std::move(outChannel));

    // 2. Input channel encapsulates external UDP network socket listening
    std::string host = config.getOr<std::string>("ingress.host", "0.0.0.0");
    int port = config.getOr<int>("ingress.port", 9000);
    int bufferSize = config.getOr<int>("ingress.buffer_size", 8 * 1024 * 1024);

    Proxy::Log::info("[UdpIngressHandler] Binding UDP network ingress on {}:{} (egress channel: '{}')...", host, port, m_outputChannel->name());

    auto udpChannel = std::make_unique<Core::Net::UdpInputChannel>(
        "udp_network_ingress",
        std::move(host),
        static_cast<std::uint16_t>(port),
        static_cast<std::size_t>(bufferSize));

    registerInputChannel(std::move(udpChannel));
    return true;
}

Core::Message::MessageResult UdpIngressHandler::onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message)
{
    if (!m_outputChannel) {
        Proxy::Log::warn("[UdpIngressHandler] No output channel is available for forwarding");
        return Core::Message::Dropped {
            .reason = "No output channel available for forwarding",
            .errorCode = 500
        };
    }

    const auto outputChannelName = m_outputChannel->name();
    Proxy::Log::debug("[UdpIngressHandler] Received datagram ({} bytes payload, {} bytes metadata) on '{}'. Forwarding to '{}'...",
        message.payload().size(), message.metadata().size(), channelName, outputChannelName);

    if (!m_outputChannel->send(message.rawBuffer())) {
        Proxy::Log::warn("[UdpIngressHandler] Failed to forward datagram to '{}'", outputChannelName);
    }
    return Core::Message::Forwarded { .channel = outputChannelName };
}

void UdpIngressHandler::onBatchReceived(
    std::string_view channelName,
    Common::Compat::span<const Core::Message::MessageView> messages,
    Common::Compat::span<Core::Message::MessageResult> outResults)
{
    const auto count = std::min(messages.size(), outResults.size());
    if (!m_outputChannel) {
        Proxy::Log::warn("[UdpIngressHandler] No output channel is available for batch forwarding");
        for (std::size_t i = 0; i < count; ++i) {
            outResults[i] = Core::Message::Dropped {
                .reason = "No output channel available for forwarding",
                .errorCode = 500
            };
        }
        return;
    }

    const auto outputChannelName = m_outputChannel->name();
    for (std::size_t i = 0; i < count; ++i) {
        outResults[i] = Core::Message::Forwarded { .channel = outputChannelName };
    }
    if (messages.empty())
        return;

    Proxy::Log::debug("[UdpIngressHandler] Received batch of {} datagram(s) on '{}'. Forwarding to '{}'...",
        messages.size(), channelName, outputChannelName);

    Common::Memory::SmallSpanBuffer spans(messages.size());
    for (std::size_t i = 0; i < messages.size(); ++i) {
        spans[i] = messages[i].rawBuffer();
    }
    m_outputChannel->sendBatch(spans.span(messages.size()));
}

} // namespace Proxy::Handler
