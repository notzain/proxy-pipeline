#pragma once

#include "backend/IBackend.hpp"
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace Proxy::Backend::Sim {

/**
 * @brief Concrete Simulation transport backend.
 *
 * Implements cross-process IPC message channels for multi-process chain simulation
 * with PCAP injection and structured MessageResult telemetry reporting.
 */
class SimBackend : public ::Backend::IBackend {
public:
    SimBackend();
    ~SimBackend() override;

    void configure(const ::Backend::IConfig& config) override;

    std::unique_ptr<::Backend::IInputChannel> createInputChannel(
        std::string_view name,
        const ::Backend::IConfig& config) override;

    std::unique_ptr<::Backend::IOutputChannel> createOutputChannel(
        std::string_view name,
        const ::Backend::IConfig& config) override;

    [[nodiscard]] bool tracksMessageResults() const noexcept override;

    void onMessageResult(
        std::string_view inputChannel,
        std::span<const std::byte> rawMessage,
        const ::Backend::MessageResult& result) override;

    void onHandlerStarted() override;

private:
    std::string m_pcapPath;
    std::string m_telemetryHost { "127.0.0.1" };
    int m_telemetryPort { 8092 };
    std::string m_eventSockPath { "/tmp/proxy_sim_events.sock" };
    std::string m_handlerName;
    int m_unixEventFd { -1 };
    int m_udpEventFd { -1 };
};

} // namespace Proxy::Backend::Sim
