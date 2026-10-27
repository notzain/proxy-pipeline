#include "core/handler/HandlerRunner.hpp"
#include "backend/IBackend.hpp"
#include "core/telemetry/StatsDExporter.hpp"
#include "core/threading/DedicatedThreadStrategy.hpp"
#include "core/threading/SequentialStrategy.hpp"
#include "core/threading/ThreadPoolStrategy.hpp"
#include "log/Log.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>

namespace {
// Global pointer to active runner instance for C signal dispatching
std::atomic<Core::Handler::HandlerRunner*> g_activeRunner { nullptr };

extern "C" void proxySignalHandler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM) {
        if (auto* runner = g_activeRunner.load(std::memory_order_acquire)) {
            runner->stop();
        }
    }
}
} // namespace

namespace Core::Handler {

HandlerRunner::HandlerRunner(IHandler& handler) noexcept
    : m_handler(handler)
{
}

HandlerRunner::~HandlerRunner()
{
    stop();
    wait();
    unregisterSignalHandler();
}

bool HandlerRunner::configure(const Config::Configuration& config)
{
    setExecutionModel(parseExecutionModel(config));
    configureTelemetry(config);
    return m_handler.configure(config);
}

void HandlerRunner::configureTelemetry(const Config::Configuration& config)
{
    std::string statsdHost = config.getOr<std::string>("telemetry.statsd.host", config.getOr<std::string>("telemetry.host", ""));
    if (!statsdHost.empty()) {
        uint16_t statsdPort = static_cast<uint16_t>(config.getOr<int>("telemetry.statsd.port", config.getOr<int>("telemetry.port", 8125)));
        std::string prefix = config.getOr<std::string>("telemetry.prefix", "proxy");
        int intervalMs = config.getOr<int>("telemetry.interval_ms", 1000);

        Proxy::Log::info("[HandlerRunner] Initializing live Telemetry StatsD exporter to {}:{} (prefix: '{}', flush interval: {}ms)...",
            statsdHost, statsdPort, prefix, intervalMs);

        m_telemetryExporter = std::make_unique<Telemetry::StatsDExporter>(statsdHost, statsdPort, prefix);
        m_telemetryExporter->start(std::chrono::milliseconds(intervalMs));
    }
}

void HandlerRunner::setExecutionModel(const ExecutionModel& model)
{
    std::visit([this](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        if constexpr (std::is_same_v<T, SequentialModel>) {
            m_strategy = std::make_unique<Threading::SequentialStrategy>();
        } else if constexpr (std::is_same_v<T, ThreadPoolModel>) {
            m_strategy = std::make_unique<Threading::ThreadPoolStrategy>(
                m.poolSize, m.strictFifo, m.serializeHandlerAccess);
        } else {
            m_strategy = std::make_unique<Threading::DedicatedThreadStrategy>(m.serializeHandlerAccess);
        }
    },
        model);
}

void HandlerRunner::setExecutionStrategy(std::unique_ptr<Threading::IExecutionStrategy> strategy)
{
    m_strategy = std::move(strategy);
}

void HandlerRunner::ensureStrategy()
{
    if (!m_strategy) {
        m_strategy = std::make_unique<Threading::DedicatedThreadStrategy>();
    }
}

void HandlerRunner::registerSignalHandler()
{
    g_activeRunner.store(this, std::memory_order_release);
    std::signal(SIGINT, proxySignalHandler);
    std::signal(SIGTERM, proxySignalHandler);
}

void HandlerRunner::unregisterSignalHandler()
{
    Core::Handler::HandlerRunner* expected = this;
    if (g_activeRunner.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) {
        std::signal(SIGINT, SIG_DFL);
        std::signal(SIGTERM, SIG_DFL);
    }
}

void HandlerRunner::start()
{
    startAsync();
    wait();
}

void HandlerRunner::startAsync()
{
    ensureStrategy();
    registerSignalHandler();

    // Check environment variables as a seamless zero-config fallback if not configured via configure()
    if (!m_telemetryExporter) {
        const char* envHost = std::getenv("PROXY_TELEMETRY_HOST");
        if (envHost && *envHost) {
            const char* envPort = std::getenv("PROXY_TELEMETRY_PORT");
            uint16_t port = envPort ? static_cast<uint16_t>(std::atoi(envPort)) : 8125;
            m_telemetryExporter = std::make_unique<Telemetry::StatsDExporter>(envHost, port, "proxy");
            m_telemetryExporter->start(std::chrono::milliseconds(1000));
        }
    }

    Proxy::Log::info("[HandlerRunner] Event loop started (channels: {}, listening for SIGINT/Ctrl+C)...",
        m_handler.inputChannelCount());
    m_strategy->start(m_handler);
    m_handler.backend().onHandlerStarted();
}

void HandlerRunner::wait()
{
    if (m_strategy) {
        m_strategy->wait();
    }
    unregisterSignalHandler();
}

void HandlerRunner::stop()
{
    if (m_strategy) {
        m_strategy->stop();
    }
}

bool HandlerRunner::isRunning() const noexcept
{
    return m_strategy && m_strategy->isRunning();
}

} // namespace Core::Handler
