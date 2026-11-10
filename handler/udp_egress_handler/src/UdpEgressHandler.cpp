#include "UdpEgressHandler.hpp"
#include "UdpEgressConfig.hpp"
#include "backend/IBackend.hpp"
#include "backend/channel/IInputChannel.hpp"
#include "backend/message/MessageView.hpp"
#include "common/memory/SmallSpanBuffer.hpp"
#include "core/config/Configuration.hpp"
#include "core/net/UdpMetadata.hpp"
#include "core/net/UdpOutputChannel.hpp"
#include "log/Log.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <utility>

namespace Proxy::Handler {

UdpEgressHandler::UdpEgressHandler(::Backend::IBackend& backend)
    : Core::Handler::IHandler(backend)
{
}

UdpEgressHandler::~UdpEgressHandler() = default;

bool UdpEgressHandler::onConfigure(const Core::Config::Configuration& config)
{
    if (!m_config.load(config)) {
        PROXY_LOG_ERROR("Configuration loading failed due to invalid parameter types");
        return false;
    }

    const std::string& inputChannelName = m_config.inputChannel;

    // 1. Create input channel through the chain's backend (e.g. SHM, UDS)
    auto inChannel = backend().createInputChannel(inputChannelName, config);
    if (!inChannel) {
        PROXY_LOG_ERROR("Failed to create input channel '{}'", inputChannelName);
        return false;
    }
    registerInputChannel(std::move(inChannel));

    // 2. Initialize UDP output socket
    m_udpOut = std::make_unique<Core::Net::UdpOutputChannel>("udp_network_egress", m_config.destHost, static_cast<std::uint16_t>(m_config.destPort));

    PROXY_LOG_INFO("Configured on channel '{}' (mode: {}, default target: {}:{})",
        inputChannelName, (m_config.mode == "forward") ? "forward" : "reply_to_origin", m_config.destHost, m_config.destPort);

    return true;
}

Backend::MessageResult UdpEgressHandler::onMessageReceived(std::string_view channelName, const Backend::MessageView& message)
{
    if (!m_udpOut) {
        return Backend::Consumed { };
    }

    auto payload = message.payload();

    if (m_config.mode != "forward") {
        const auto meta = message.getMetadata<Core::Net::UdpMetadata>();
        if (meta && meta->srcPort != 0) {
            const auto originAddr = (meta->addressFamily == 6)
                ? Common::Net::SocketAddress::fromIpv6Net(meta->srcIpv6, meta->srcPort)
                : Common::Net::SocketAddress::fromIpv4Net(meta->srcIp, meta->srcPort);

            if (Proxy::Log::shouldLog(Proxy::Log::Level::Debug)) {
                PROXY_LOG_DEBUG("Channel '{}': Replying {} payload bytes directly to origin client {}",
                    channelName, payload.size(), originAddr.toString());
            }

            if (!m_udpOut->sendTo(payload, originAddr)) {
                PROXY_LOG_WARN("Failed to send reply to origin client {}", originAddr.toString());
            }
            return Backend::Consumed { };
        }
    }

    // Default or forward mode
    PROXY_LOG_DEBUG("Channel '{}': Forwarding {} payload bytes to {}:{}",
        channelName, payload.size(), m_config.destHost, m_config.destPort);

    if (!m_udpOut->send(payload)) {
        PROXY_LOG_WARN("Failed to forward packet to {}:{}", m_config.destHost, m_config.destPort);
    }
    return Backend::Consumed { };
}

void UdpEgressHandler::onBatchReceived(
    std::string_view channelName,
    Common::Compat::span<const Backend::MessageView> messages,
    Common::Compat::span<Backend::MessageResult> outResults)
{
    const auto count = std::min(messages.size(), outResults.size());
    if (count == 0) {
        return;
    }

    if (m_config.mode == "forward" && m_udpOut) {
        Common::Memory::SmallSpanBuffer<Common::Compat::span<const std::byte>, 64> payloads(count);
        for (std::size_t i = 0; i < count; ++i) {
            payloads[i] = messages[i].payload();
            outResults[i] = Backend::Consumed { };
        }
        m_udpOut->sendBatch(payloads.constSpan());
        return;
    }

    for (std::size_t i = 0; i < count; ++i) {
        outResults[i] = onMessageReceived(channelName, messages[i]);
    }
}

} // namespace Proxy::Handler
