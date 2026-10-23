#include "core/handler/IHandler.hpp"
#include "core/message/MessageView.hpp"
#include "core/telemetry/MetricsRegistry.hpp"
#include "core/telemetry/StatsDExporter.hpp"
#include "core/threading/DedicatedThreadStrategy.hpp"
#include "core/threading/SequentialStrategy.hpp"
#include "core/threading/ThreadPoolStrategy.hpp"
#include "log/Log.hpp"
#include <array>
#include <chrono>
#include <csignal>
#include <mutex>

namespace {
// Global pointer to active handler instance for C signal dispatching
std::atomic<Core::Handler::IHandler*> g_activeHandler { nullptr };

extern "C" void proxySignalHandler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM) {
        if (auto* handler = g_activeHandler.load(std::memory_order_acquire)) {
            handler->stop();
        }
    }
}
} // namespace

namespace Core::Handler {

ExecutionModel parseExecutionModel(std::string_view name)
{
    if (name == "sequential" || name == "single") {
        return ExecutionModel::Sequential;
    }
    if (name == "thread_pool" || name == "pool") {
        return ExecutionModel::ThreadPool;
    }
    return ExecutionModel::Dedicated;
}

IHandler::~IHandler()
{
    stop();
    wait();
    unregisterSignalHandler();
}

bool IHandler::configure(const Config::Configuration& config, ::Backend::IBackend&)
{
    // Auto-configure telemetry StatsD exporter if telemetry.statsd.host or telemetry.host is set
    std::string statsdHost = config.getOr<std::string>("telemetry.statsd.host", config.getOr<std::string>("telemetry.host", ""));
    if (!statsdHost.empty()) {
        uint16_t statsdPort = static_cast<uint16_t>(config.getOr<int>("telemetry.statsd.port", config.getOr<int>("telemetry.port", 8125)));
        std::string prefix = config.getOr<std::string>("telemetry.prefix", "proxy");
        int intervalMs = config.getOr<int>("telemetry.interval_ms", 1000);

        Proxy::Log::info("[IHandler] Initializing live Telemetry StatsD exporter to {}:{} (prefix: '{}', flush interval: {}ms)...",
            statsdHost, statsdPort, prefix, intervalMs);

        m_telemetryExporter = std::make_unique<Telemetry::StatsDExporter>(statsdHost, statsdPort, prefix);
        m_telemetryExporter->start(std::chrono::milliseconds(intervalMs));
    }
    return true;
}

::Backend::IInputChannel* IHandler::registerInputChannel(std::string_view channelName, std::unique_ptr<::Backend::IInputChannel> channel)
{
    if (!channel) {
        Proxy::Log::warn("[IHandler] Attempted to register null input channel '{}'", channelName);
        return nullptr;
    }
    auto* rawPtr = channel.get();
    m_inputChannels.push_back(NamedInputChannel { std::string(channelName), std::move(channel) });
    return rawPtr;
}

const std::vector<IHandler::NamedInputChannel>& IHandler::getInputChannels() const noexcept
{
    return m_inputChannels;
}

void IHandler::onMessageReceived(std::string_view, const Message::MessageView&)
{
}

void IHandler::onBatchReceived(std::string_view channelName, std::span<const Message::MessageView> messages)
{
    auto& latencyHist = Telemetry::MetricsRegistry::instance().latencyHistogram(
        "message_processing_duration_us", "Message transit and processing duration in microseconds");

    uint64_t nowNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch())
            .count());

    for (const auto& msg : messages) {
        if (msg.timestampNs() > 0 && nowNs >= msg.timestampNs()) {
            double durationUs = static_cast<double>(nowNs - msg.timestampNs()) / 1000.0;
            latencyHist.observe(durationUs);
        }
        onMessageReceived(channelName, msg);
    }
}

::Backend::IOutputChannel* IHandler::registerOutputChannel(std::string_view channelName, std::unique_ptr<::Backend::IOutputChannel> channel)
{
    if (!channel) {
        Proxy::Log::warn("[IHandler] Attempted to register null output channel '{}'", channelName);
        return nullptr;
    }
    std::unique_lock lock(m_outputMutex);
    auto* rawPtr = channel.get();
    m_outputChannels[std::string(channelName)] = std::move(channel);
    return rawPtr;
}

void IHandler::step()
{
}

void IHandler::setExecutionModel(ExecutionModel model, std::size_t poolSize, bool strictFifo)
{
    switch (model) {
    case ExecutionModel::Sequential:
        m_strategy = std::make_unique<Threading::SequentialStrategy>();
        break;
    case ExecutionModel::ThreadPool:
        m_strategy = std::make_unique<Threading::ThreadPoolStrategy>(poolSize, strictFifo);
        break;
    case ExecutionModel::Dedicated:
    default:
        m_strategy = std::make_unique<Threading::DedicatedThreadStrategy>();
        break;
    }
}

void IHandler::setExecutionStrategy(std::unique_ptr<Threading::IExecutionStrategy> strategy)
{
    m_strategy = std::move(strategy);
}

void IHandler::ensureStrategy()
{
    if (!m_strategy) {
        m_strategy = std::make_unique<Threading::DedicatedThreadStrategy>();
    }
}

void IHandler::registerSignalHandler()
{
    g_activeHandler.store(this, std::memory_order_release);
    std::signal(SIGINT, proxySignalHandler);
    std::signal(SIGTERM, proxySignalHandler);
}

void IHandler::unregisterSignalHandler()
{
    Core::Handler::IHandler* expected = this;
    if (g_activeHandler.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) {
        std::signal(SIGINT, SIG_DFL);
        std::signal(SIGTERM, SIG_DFL);
    }
}

void IHandler::start()
{
    startAsync();
    wait();
}

void IHandler::startAsync()
{
    ensureStrategy();
    registerSignalHandler();

    // Auto-initialize telemetry StatsD exporter if telemetry.statsd.host or telemetry.host is set
    // This ensures telemetry starts even if derived classes override configure() without calling IHandler::configure()
    if (!m_telemetryExporter) {
        // Check environment variables as well for seamless zero-config overrides
        const char* envHost = std::getenv("PROXY_TELEMETRY_HOST");
        if (envHost && *envHost) {
            const char* envPort = std::getenv("PROXY_TELEMETRY_PORT");
            uint16_t port = envPort ? static_cast<uint16_t>(std::atoi(envPort)) : 8125;
            m_telemetryExporter = std::make_unique<Telemetry::StatsDExporter>(envHost, port, "proxy");
            m_telemetryExporter->start(std::chrono::milliseconds(1000));
        }
    }

    Proxy::Log::info("[IHandler] Event loop started (channels: {}, listening for SIGINT/Ctrl+C)...", m_inputChannels.size());
    m_strategy->start(*this);
}

void IHandler::wait()
{
    if (m_strategy) {
        m_strategy->wait();
    }
    unregisterSignalHandler();
}

void IHandler::stop()
{
    if (m_strategy) {
        m_strategy->stop();
    }
}

bool IHandler::isRunning() const noexcept
{
    return m_strategy && m_strategy->isRunning();
}

} // namespace Core::Handler
