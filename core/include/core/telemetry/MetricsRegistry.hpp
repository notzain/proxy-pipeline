#pragma once

#include "core/telemetry/Counter.hpp"
#include "core/telemetry/Gauge.hpp"
#include "core/telemetry/Histogram.hpp"
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Core::Telemetry {

/**
 * @brief Thread-safe central registry of application telemetry metrics.
 *
 * Metrics are stored by value in internal containers without heap allocations via unique_ptr.
 */
class MetricsRegistry {
public:
    static MetricsRegistry& instance();

    MetricsRegistry();
    ~MetricsRegistry();

    MetricsRegistry(const MetricsRegistry&) = delete;
    MetricsRegistry& operator=(const MetricsRegistry&) = delete;

    /**
     * @brief Gets or registers a Counter.
     */
    Counter& counter(std::string name, std::string help = "");

    /**
     * @brief Gets or registers a Gauge.
     */
    Gauge& gauge(std::string name, std::string help = "");

    /**
     * @brief Gets or registers a Histogram with custom bucket bounds.
     */
    Histogram& histogram(std::string name, std::string help, std::span<const double> bucketBounds);

    /**
     * @brief Gets or registers a default latency Histogram.
     */
    Histogram& latencyHistogram(std::string name, std::string help = "");

    /**
     * @brief Formats all registered metrics into Prometheus exposition text format.
     */
    [[nodiscard]] std::string toPrometheus() const;

    /**
     * @brief Clears all registered metrics.
     */
    void clear();

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, Counter> m_counters;
    std::unordered_map<std::string, Gauge> m_gauges;
    std::unordered_map<std::string, Histogram> m_histograms;
};

} // namespace Core::Telemetry
