#pragma once

#include "backend/IBackend.hpp"
#include <memory>
#include <string>

namespace Proxy::Backend::Sim {

/**
 * @brief Concrete Simulation transport backend.
 *
 * Implements cross-process IPC message channels for multi-process chain simulation
 * with optional PCAP injection and simulation event telemetry reporting.
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

private:
    std::string m_pcapPath;
    std::string m_telemetryHost { "127.0.0.1" };
    int m_telemetryPort { 8092 };
    std::string m_handlerName;
};

} // namespace Proxy::Backend::Sim
