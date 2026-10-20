#pragma once

#include "core/handler/IHandler.hpp"
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace Backend {
class IBackend;
class IOutputChannel;
} // namespace Backend

namespace Proxy::Handler {

/**
 * @brief Ingress handler that receives external UDP datagram streams and forwards raw payloads
 * into the pipeline transport backend (e.g. Shared Memory, Unix Domain Sockets, etc.).
 *
 * Responsibilities:
 * - Listens on an external UDP host and port.
 * - Extracts complete datagram payloads without network headers or transport envelopes.
 * - Forwards payloads directly to the registered output channel configured via the active backend.
 */
class UdpIngressHandler : public Core::Handler::IHandler {
public:
    UdpIngressHandler() = default;
    ~UdpIngressHandler() override = default;

    /**
     * @brief Configures the UDP ingress listener and output channel from configuration.
     *
     * Reads:
     * - "ingress.host" (default: "0.0.0.0")
     * - "ingress.port" (default: 9000)
     * - "ingress.buffer_size" (default: 8388608 / 8MB)
     * - "ingress.output_channel" or "channel.output" (name of backend egress channel)
     *
     * @param config Strongly-typed configuration store.
     * @param backend Active pipeline transport backend for egress.
     * @return true If configuration and socket binding succeeded, false otherwise.
     */
    bool configure(const Core::Config::Configuration& config, ::Backend::IBackend& backend) override;

    /**
     * @brief Dispatches raw incoming datagram payloads directly into the backend output channel.
     *
     * @param channelName Name of the input channel ("udp_network_ingress").
     * @param buffer Non-owning view of the raw received payload.
     */
    void onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer) override;

    /**
     * @brief Vectorized dispatch of multiple received datagram payloads into the backend output channel.
     *
     * @param channelName Name of the input channel.
     * @param messages Vectorized view of received payload buffers.
     */
    void onBatchReceived(std::string_view channelName, std::span<const std::span<const std::byte>> messages) override;

private:
    std::string m_outputChannelName;
    ::Backend::IOutputChannel* m_outputChannel { nullptr };
};

} // namespace Proxy::Handler
