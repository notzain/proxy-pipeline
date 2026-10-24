#include "UdpEgressHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/IInputChannel.hpp"
#include "core/config/Configuration.hpp"
#include "core/message/MessageView.hpp"
#include "core/message/UdpMetadata.hpp"
#include "core/net/UdpOutputChannel.hpp"
#include "log/Log.hpp"
#include <arpa/inet.h>
#include <utility>

namespace Proxy::Handler {

UdpEgressHandler::UdpEgressHandler() = default;
UdpEgressHandler::~UdpEgressHandler() = default;

bool UdpEgressHandler::configure(const Core::Config::Configuration& config, ::Backend::IBackend& backend)
{
    IHandler::configure(config, backend);

    m_inputChannelName = config.getOr<std::string>(
        "egress.input_channel",
        config.getOr<std::string>(
            "channel.input",
            config.getOr<std::string>("print.channel", "egress_in")));

    std::string mode = config.getOr<std::string>("egress.mode", "reply_to_origin");
    m_replyToOrigin = (mode != "forward");

    m_destHost = config.getOr<std::string>("egress.dest_host", "127.0.0.1");
    m_destPort = static_cast<std::uint16_t>(config.getOr<int>("egress.dest_port", 9200));

    // 1. Create input channel through the chain's backend (e.g. SHM, UDS)
    auto inChannel = backend.createInputChannel(m_inputChannelName, config);
    if (!inChannel) {
        Proxy::Log::error("[UdpEgressHandler] Failed to create input channel '{}'", m_inputChannelName);
        return false;
    }
    m_inputChannel = registerInputChannel(m_inputChannelName, std::move(inChannel));

    // 2. Initialize UDP output socket
    m_udpOut = std::make_unique<Core::Net::UdpOutputChannel>(m_destHost, m_destPort);

    Proxy::Log::info("[UdpEgressHandler] Configured on channel '{}' (mode: {}, default target: {}:{})",
        m_inputChannelName, m_replyToOrigin ? "reply_to_origin" : "forward", m_destHost, m_destPort);

    return true;
}

Core::Message::MessageResult UdpEgressHandler::onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message)
{
    if (!m_udpOut) {
        return Core::Message::Consumed { };
    }

    auto payload = message.payload();

    if (m_replyToOrigin && message.protocol() == Core::Message::ProtocolId::Udp) {
        const auto* meta = message.getMetadata<Core::Message::UdpMetadata>();
        if (meta && meta->srcPort != 0) {
            const auto originAddr = (meta->addressFamily == 6)
                ? ::Backend::SocketAddress::fromIpv6Net(meta->srcIpv6, meta->srcPort)
                : ::Backend::SocketAddress::fromIpv4Net(meta->srcIp, meta->srcPort);

            if (Proxy::Log::shouldLog(Proxy::Log::Level::Debug)) {
                Proxy::Log::debug("[UdpEgressHandler] Channel '{}': Replying {} payload bytes directly to origin client {}",
                    channelName, payload.size(), originAddr.toString());
            }

            if (!m_udpOut->sendTo(payload, originAddr)) {
                Proxy::Log::warn("[UdpEgressHandler] Failed to send reply to origin client {}", originAddr.toString());
            }
            return Core::Message::Consumed { };
        }
    }

    // Default or forward mode
    Proxy::Log::debug("[UdpEgressHandler] Channel '{}': Forwarding {} payload bytes to {}:{}",
        channelName, payload.size(), m_destHost, m_destPort);

    if (!m_udpOut->send(payload)) {
        Proxy::Log::warn("[UdpEgressHandler] Failed to forward packet to {}:{}", m_destHost, m_destPort);
    }
    return Core::Message::Consumed { };
}

void UdpEgressHandler::onBatchReceived(std::string_view channelName, std::span<const Core::Message::MessageView> messages)
{
    for (const auto& msg : messages) {
        onMessageReceived(channelName, msg);
    }
}

} // namespace Proxy::Handler
