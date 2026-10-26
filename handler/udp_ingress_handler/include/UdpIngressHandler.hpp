#pragma once

#include "core/handler/IHandler.hpp"
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
    using Core::Handler::IHandler::IHandler;
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
     * @return true If configuration and socket binding succeeded, false otherwise.
     */
    bool onConfigure(const Core::Config::Configuration& config) override;

    [[nodiscard]] Core::Handler::HandlerRole role() const noexcept override { return Core::Handler::HandlerRole::ExternalIngress; }
    [[nodiscard]] Core::Handler::HandlerProtocol protocol() const noexcept override { return Core::Handler::HandlerProtocol::Udp; }

    /**
     * @brief Dispatches raw incoming datagram payloads directly into the backend output channel.
     *
     * @param channelName Name of the input channel ("udp_network_ingress").
     * @param buffer Non-owning view of the raw received payload.
     */
    Core::Message::MessageResult onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message) override;
    void onBatchReceived(
        std::string_view channelName,
        std::span<const Core::Message::MessageView> messages,
        std::span<Core::Message::MessageResult> outResults) override;

private:
    Core::Memory::ObserverPtr<::Backend::IOutputChannel> m_outputChannel;
};

} // namespace Proxy::Handler
