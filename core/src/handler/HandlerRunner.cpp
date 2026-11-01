#include "core/handler/HandlerRunner.hpp"
#include "backend/IBackend.hpp"
#include "core/handler/PosixSignalManager.hpp"
#include "core/telemetry/StatsDExporter.hpp"
#include "core/threading/DedicatedThreadStrategy.hpp"
#include "core/threading/SequentialStrategy.hpp"
#include "core/threading/SimpleExecutionStrategy.hpp"
#include "core/threading/ThreadPoolStrategy.hpp"
#include "log/Log.hpp"
#include <chrono>
#include <cstdlib>
#include <utility>

namespace Core::Handler {

HandlerRunner::HandlerRunner(IHandler& handler) noexcept
    : m_handler(handler)
{
}

HandlerRunner::~HandlerRunner()
{
    stop();
    wait();
    PosixSignalManager::release(this);
}

bool HandlerRunner::configure(const Config::Configuration& config)
{
    const auto defaultPool = m_handler.backend().bufferPool();
    const auto blockSize = static_cast<std::size_t>(
        config.getOr<std::uint64_t>("memory.pool_block_size", defaultPool->blockSize()));
    const auto blockCount = static_cast<std::size_t>(
        config.getOr<std::uint64_t>("memory.pool_block_count", defaultPool->blockCount()));
    if (blockSize != defaultPool->blockSize() || blockCount != defaultPool->blockCount()) {
        m_handler.backend().setBufferPool(std::make_shared<Common::Memory::BufferPool>(
            blockSize > 0 ? blockSize : Common::Memory::BufferPool::DefaultBlockSize,
            blockCount > 0 ? blockCount : Common::Memory::BufferPool::DefaultBlockCount));
    }

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
        PosixSignalManager::acquire(this);
    }
    try {
        Proxy::Log::info("[HandlerRunner] Event loop started (channels: {}, listening for SIGINT/Ctrl+C)...",
            m_handler.inputChannelCount());
        m_strategy->start(m_handler);
        m_shutdownThread = Common::Compat::jthread([this, registerSignals](Common::Compat::stop_token stopToken) {
            while (!stopToken.stop_requested()) {
                if (m_stopRequested.load(std::memory_order_acquire)
                    || (registerSignals && PosixSignalManager::isShutdownRequested())) {
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
            PosixSignalManager::release(this);
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
    PosixSignalManager::release(this);
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
