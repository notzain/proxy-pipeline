#include "UdpIngressHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/IOutputChannel.hpp"
#include "core/config/Configuration.hpp"
#include "core/net/UdpInputChannel.hpp"
#include "log/Log.hpp"
#include <utility>

namespace Proxy::Handler {

bool UdpIngressHandler::configure(const Core::Config::Configuration& config, ::Backend::IBackend& backend)
{
    m_outputChannelName = config.getOr<std::string>(
        "ingress.output_channel",
        config.getOr<std::string>(
            "channel.output",
            config.getOr<std::string>(
                "output_channel",
                config.getOr<std::string>(
                    "dummy.channel",
                    config.getOr<std::string>("xml.output_channel", "ingress_out")))));

    // 1. Output channel is created through the active backend (e.g. SHM, UDS, etc.)
    auto outChannel = backend.createOutputChannel(m_outputChannelName, config);
    if (!outChannel) {
        Proxy::Log::error("[UdpIngressHandler] Failed to create pipeline output channel '{}'", m_outputChannelName);
        return false;
    }
    m_outputChannel = registerOutputChannel(m_outputChannelName, std::move(outChannel));

    // 2. Input channel encapsulates external UDP network socket listening
    std::string host = config.getOr<std::string>("ingress.host", "0.0.0.0");
    int port = config.getOr<int>("ingress.port", 9000);
    int bufferSize = config.getOr<int>("ingress.buffer_size", 8 * 1024 * 1024);

    Proxy::Log::info("[UdpIngressHandler] Binding UDP network ingress on {}:{} (egress channel: '{}')...", host, port, m_outputChannelName);

    auto udpChannel = std::make_unique<Core::Net::UdpInputChannel>(
        std::move(host),
        static_cast<std::uint16_t>(port),
        static_cast<std::size_t>(bufferSize));

    registerInputChannel("udp_network_ingress", std::move(udpChannel));
    return true;
}

void UdpIngressHandler::onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer)
{
    Proxy::Log::info("[UdpIngressHandler] Received datagram ({} bytes) on '{}'. Forwarding to '{}'...",
        buffer.size(), channelName, m_outputChannelName);

    if (m_outputChannel) {
        if (!m_outputChannel->send(buffer)) {
            Proxy::Log::warn("[UdpIngressHandler] Failed to forward datagram to '{}'", m_outputChannelName);
        }
    } else {
        if (!send(m_outputChannelName, buffer)) {
            Proxy::Log::warn("[UdpIngressHandler] Failed to send datagram to '{}'", m_outputChannelName);
        }
    }
}

void UdpIngressHandler::onBatchReceived(std::string_view channelName, std::span<const std::span<const std::byte>> messages)
{
    Proxy::Log::info("[UdpIngressHandler] Received batch of {} datagram(s) on '{}'. Forwarding to '{}'...",
        messages.size(), channelName, m_outputChannelName);

    if (m_outputChannel) {
        m_outputChannel->sendBatch(messages);
    } else {
        sendBatch(m_outputChannelName, messages);
    }
}

} // namespace Proxy::Handler
