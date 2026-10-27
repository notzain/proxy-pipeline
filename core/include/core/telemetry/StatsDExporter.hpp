#pragma once

#include "common/io/UniqueFd.hpp"
#include "core/telemetry/MetricsRegistry.hpp"
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace Core::Telemetry {

/**
 * @brief Exporter sending telemetry metrics as StatsD UDP datagrams over the network.
 */
class StatsDExporter {
public:
    /**
     * @brief Constructs exporter for target StatsD / DogStatsD agent.
     *
     * @param host Target host (IPv4, e.g. "127.0.0.1").
     * @param port Target UDP port (typically 8125).
     * @param prefix Metric prefix prepended to all metric names (e.g. "proxy").
     * @param registry Reference to metrics registry to scrape.
     */
    StatsDExporter(
        std::string host,
        uint16_t port,
        std::string prefix = "proxy",
        MetricsRegistry& registry = MetricsRegistry::instance());

    ~StatsDExporter();

    StatsDExporter(const StatsDExporter&) = delete;
    StatsDExporter& operator=(const StatsDExporter&) = delete;

    /**
     * @brief Starts periodic flushing thread.
     *
     * @param interval Flush interval.
     */
    void start(std::chrono::milliseconds interval = std::chrono::seconds(1));

    /**
     * @brief Stops periodic flushing thread.
     */
    void stop();

    /**
     * @brief Immediately serializes and transmits current registry metrics over UDP.
     */
    void flushNow();

    /**
     * @brief Sends a standalone metric directly to StatsD without registering.
     */
    void sendCount(std::string_view metric, int64_t count);
    void sendGauge(std::string_view metric, int64_t value);
    void sendTiming(std::string_view metric, double milliseconds);

private:
    std::string m_host;
    uint16_t m_port;
    std::string m_prefix;
    MetricsRegistry& m_registry;

    Common::Io::UniqueFd m_socketFd;
    std::atomic<bool> m_running { false };
    std::thread m_worker;

    void initSocket();
    void sendRaw(std::string_view payload);
};

} // namespace Core::Telemetry
