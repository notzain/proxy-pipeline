#include "backend/sim/SimBackend.hpp"
#include "SimInputChannel.hpp"
#include "SimOutputChannel.hpp"
#include "log/ConsoleLogSink.hpp"
#include "log/Log.hpp"
#include <fmt/core.h>
#include <memory>

namespace Proxy::Backend::Sim {

SimBackend::SimBackend() = default;

SimBackend::~SimBackend() = default;

void SimBackend::configure(const ::Backend::IConfig& config)
{
    m_pcapPath = config.getOr<std::string>("pcap.path", config.getOr<std::string>("sim.pcap", ""));
    m_telemetryHost = config.getOr<std::string>("sim.event_host", "127.0.0.1");
    m_telemetryPort = config.getOr<int>("sim.event_port", 8092);
    m_handlerName = config.getOr<std::string>("sim.handler_name", config.getOr<std::string>("telemetry.prefix", "handler"));

    bool logConsole = config.getOr<bool>("log.console", true);
    if (logConsole) {
        Proxy::Log::init(std::make_unique<Proxy::Log::ConsoleLogSink>());
    }
}

std::unique_ptr<::Backend::IInputChannel> SimBackend::createInputChannel(
    std::string_view name,
    const ::Backend::IConfig& config)
{
    std::string defaultSock = fmt::format("/tmp/proxy_sim_{}.sock", name);
    std::string sockPath = config.getOr<std::string>(fmt::format("channel.{}.path", name), defaultSock);

    return std::make_unique<SimInputChannel>(
        std::string(name),
        std::move(sockPath),
        m_telemetryHost,
        m_telemetryPort,
        m_handlerName);
}

std::unique_ptr<::Backend::IOutputChannel> SimBackend::createOutputChannel(
    std::string_view name,
    const ::Backend::IConfig& config)
{
    std::string defaultSock = fmt::format("/tmp/proxy_sim_{}.sock", name);
    std::string defaultDest = fmt::format("/tmp/proxy_sim_hold_{}.sock", name);
    std::string sockPath = config.getOr<std::string>(fmt::format("channel.{}.dest_path", name),
        config.getOr<std::string>(fmt::format("channel.{}.path", name), defaultDest));

    return std::make_unique<SimOutputChannel>(
        std::string(name),
        std::move(sockPath),
        m_telemetryHost,
        m_telemetryPort,
        m_handlerName);
}

} // namespace Proxy::Backend::Sim

namespace Backend {

[[gnu::weak]] std::unique_ptr<IBackend> createBackend()
{
    return std::make_unique<Proxy::Backend::Sim::SimBackend>();
}

} // namespace Backend
