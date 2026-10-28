#include "core/handler/HandlerRunner.hpp"
#include "backend/IBackend.hpp"
#include "core/telemetry/StatsDExporter.hpp"
#include "core/threading/DedicatedThreadStrategy.hpp"
#include "core/threading/SequentialStrategy.hpp"
#include "core/threading/SimpleExecutionStrategy.hpp"
#include "core/threading/ThreadPoolStrategy.hpp"
#include "log/Log.hpp"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <system_error>

namespace {
static_assert(std::atomic<bool>::is_always_lock_free);

std::atomic<bool> g_shutdownRequested { false };
std::mutex g_signalOwnershipMutex;
Core::Handler::HandlerRunner* g_activeRunner { nullptr };
struct sigaction g_previousSigintAction { };
struct sigaction g_previousSigtermAction { };

extern "C" void proxySignalHandler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM) {
        g_shutdownRequested.store(true, std::memory_order_relaxed);
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
    model.match(
        [this](const SimpleModel&) {
            m_strategy = std::make_unique<Threading::SimpleExecutionStrategy>();
        },
        [this](const SequentialModel&) {
            m_strategy = std::make_unique<Threading::SequentialStrategy>();
        },
        [this](const ThreadPoolModel& m) {
            m_strategy = std::make_unique<Threading::ThreadPoolStrategy>(
                m.poolSize, m.strictFifo, m.serializeHandlerAccess);
        },
        [this](const DedicatedThreadModel& m) {
            m_strategy = std::make_unique<Threading::DedicatedThreadStrategy>(m.serializeHandlerAccess);
        });
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
    std::lock_guard lock(g_signalOwnershipMutex);
    if (g_activeRunner) {
        throw std::logic_error("Signal handling is already owned by another HandlerRunner");
    }

    g_shutdownRequested.store(false, std::memory_order_relaxed);

    struct sigaction action { };
    action.sa_handler = proxySignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction(SIGINT, &action, &g_previousSigintAction) != 0) {
        throw std::system_error(errno, std::generic_category(), "Failed to register SIGINT handler");
    }

    if (sigaction(SIGTERM, &action, &g_previousSigtermAction) != 0) {
        const int error = errno;
        if (sigaction(SIGINT, &g_previousSigintAction, nullptr) != 0) {
            Proxy::Log::error("[HandlerRunner] Failed to restore previous SIGINT handler: {}", std::strerror(errno));
        }
        throw std::system_error(error, std::generic_category(), "Failed to register SIGTERM handler");
    }

    g_activeRunner = this;
}

void HandlerRunner::unregisterSignalHandler()
{
    if (m_shutdownThread.joinable()) {
        m_shutdownThread.join();
    }

    std::lock_guard lock(g_signalOwnershipMutex);
    if (g_activeRunner != this) {
        return;
    }

    if (sigaction(SIGINT, &g_previousSigintAction, nullptr) != 0) {
        Proxy::Log::error("[HandlerRunner] Failed to restore previous SIGINT handler: {}", std::strerror(errno));
    }
    if (sigaction(SIGTERM, &g_previousSigtermAction, nullptr) != 0) {
        Proxy::Log::error("[HandlerRunner] Failed to restore previous SIGTERM handler: {}", std::strerror(errno));
    }
    g_activeRunner = nullptr;
    g_shutdownRequested.store(false, std::memory_order_relaxed);
}

void HandlerRunner::start()
{
    startAsync();
    wait();
}

void HandlerRunner::startAsync(bool registerSignals)
{
    ensureStrategy();
    m_stopRequested.store(false, std::memory_order_release);

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

    if (registerSignals) {
        registerSignalHandler();
    }
    try {
        Proxy::Log::info("[HandlerRunner] Event loop started (channels: {}, listening for SIGINT/Ctrl+C)...",
            m_handler.inputChannelCount());
        m_strategy->start(m_handler);
        m_shutdownThread = Common::Compat::jthread([this, registerSignals](Common::Compat::stop_token stopToken) {
            while (!stopToken.stop_requested()) {
                if (m_stopRequested.load(std::memory_order_acquire)
                    || (registerSignals && g_shutdownRequested.load(std::memory_order_relaxed))) {
                    m_strategy->stop();
                    break;
                }
                if (!m_strategy->isRunning()) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            m_strategy->wait();
        });
    } catch (...) {
        m_strategy->stop();
        m_strategy->wait();
        if (registerSignals) {
            unregisterSignalHandler();
        }
        throw;
    }

    m_handler.backend().onHandlerStarted();
}

void HandlerRunner::wait()
{
    if (m_shutdownThread.joinable()) {
        m_shutdownThread.join();
    } else if (m_strategy) {
        m_strategy->wait();
    }
    unregisterSignalHandler();
}

void HandlerRunner::stop()
{
    m_stopRequested.store(true, std::memory_order_release);
}

bool HandlerRunner::isRunning() const noexcept
{
    return m_strategy && m_strategy->isRunning();
}

} // namespace Core::Handler
