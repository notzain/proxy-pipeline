#pragma once

#include "core/handler/IHandler.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace Backend {
class IBackend;
class IInputChannel;
} // namespace Backend

namespace Core::Net {
class UdpOutputChannel;
} // namespace Core::Net

namespace Proxy::Handler {

/**
 * @brief Egress handler that terminates the pipeline and transmits outgoing messages
 * over UDP datagrams to the external network.
 *
 * Can operate in two modes:
 * 1. "reply_to_origin": Extracts originating client IP and port from UdpMetadata,
 *    recovering the network context stripped at ingress and replying directly to the client.
 * 2. "forward": Transmits to a statically configured upstream destination host:port.
 */
class UdpEgressHandler : public Core::Handler::IHandler {
public:
    UdpEgressHandler();
    ~UdpEgressHandler() override;

    /**
     * @brief Configures the UDP egress handler and input channel.
     *
     * Reads:
     * - "egress.mode" ("reply_to_origin" [default] or "forward")
     * - "egress.dest_host" (default: "127.0.0.1")
     * - "egress.dest_port" (default: 9200)
     * - "egress.input_channel" or "channel.input" (name of backend ingress channel)
     *
     * @param config Strongly-typed configuration store.
     * @param backend Active pipeline transport backend.
     * @return true If configuration succeeded, false otherwise.
     */
    bool configure(const Core::Config::Configuration& config, ::Backend::IBackend& backend) override;

    /**
     * @brief Ingress callback for enveloped messages received from the internal pipeline.
     * Restores client destination from UdpMetadata and transmits message.payload() via UDP.
     */
    void onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message) override;

    /**
     * @brief Vectorized ingress callback for batches of enveloped messages.
     */
    void onBatchReceived(std::string_view channelName, std::span<const Core::Message::MessageView> messages) override;

private:
    std::string m_inputChannelName;
    ::Backend::IInputChannel* m_inputChannel { nullptr };
    std::unique_ptr<Core::Net::UdpOutputChannel> m_udpOut;

    std::string m_destHost { "127.0.0.1" };
    std::uint16_t m_destPort { 9200 };
    bool m_replyToOrigin { true };
};

} // namespace Proxy::Handler
