#include "core/telemetry/MetricsRegistry.hpp"
#include <fmt/core.h>

namespace Core::Telemetry {

MetricsRegistry& MetricsRegistry::instance()
{
    static MetricsRegistry s_instance;
    return s_instance;
}

MetricsRegistry::MetricsRegistry() = default;
MetricsRegistry::~MetricsRegistry() = default;

Counter& MetricsRegistry::counter(std::string name, std::string help)
{
    std::lock_guard lock(m_mutex);
    auto it = m_counters.find(name);
    if (it != m_counters.end()) {
        return it->second;
    }
    std::string key = name;
    auto [insertedIt, _] = m_counters.try_emplace(std::move(key), std::move(name), std::move(help));
    return insertedIt->second;
}

Gauge& MetricsRegistry::gauge(std::string name, std::string help)
{
    std::lock_guard lock(m_mutex);
    auto it = m_gauges.find(name);
    if (it != m_gauges.end()) {
        return it->second;
    }
    std::string key = name;
    auto [insertedIt, _] = m_gauges.try_emplace(std::move(key), std::move(name), std::move(help));
    return insertedIt->second;
}

Histogram& MetricsRegistry::histogram(std::string name, std::string help, Common::Compat::span<const double> bucketBounds)
{
    std::lock_guard lock(m_mutex);
    auto it = m_histograms.find(name);
    if (it != m_histograms.end()) {
        return it->second;
    }
    std::string key = name;
    auto [insertedIt, _] = m_histograms.try_emplace(std::move(key), std::move(name), std::move(help), bucketBounds);
    return insertedIt->second;
}

Histogram& MetricsRegistry::latencyHistogram(std::string name, std::string help)
{
    std::lock_guard lock(m_mutex);
    auto it = m_histograms.find(name);
    if (it != m_histograms.end()) {
        return it->second;
    }
    std::string key = name;
    auto [insertedIt, _] = m_histograms.try_emplace(std::move(key), Histogram::defaultLatency(std::move(name), std::move(help)));
    return insertedIt->second;
}

void MetricsRegistry::clear()
{
    std::lock_guard lock(m_mutex);
    m_counters.clear();
    m_gauges.clear();
    m_histograms.clear();
}

std::string MetricsRegistry::toPrometheus() const
{
    std::lock_guard lock(m_mutex);
    std::string out;
    out.reserve(4096);

    // Export Counters
    for (const auto& [name, c] : m_counters) {
        if (!c.help().empty()) {
            fmt::format_to(std::back_inserter(out), "# HELP {} {}\n", name, c.help());
        }
        fmt::format_to(std::back_inserter(out), "# TYPE {} counter\n", name);
        fmt::format_to(std::back_inserter(out), "{} {}\n", name, c.value());
    }

    // Export Gauges
    for (const auto& [name, g] : m_gauges) {
        if (!g.help().empty()) {
            fmt::format_to(std::back_inserter(out), "# HELP {} {}\n", name, g.help());
        }
        fmt::format_to(std::back_inserter(out), "# TYPE {} gauge\n", name);
        fmt::format_to(std::back_inserter(out), "{} {}\n", name, g.value());
    }

    // Export Histograms
    for (const auto& [name, h] : m_histograms) {
        if (!h.help().empty()) {
            fmt::format_to(std::back_inserter(out), "# HELP {} {}\n", name, h.help());
        }
        fmt::format_to(std::back_inserter(out), "# TYPE {} histogram\n", name);

        auto snap = h.snapshot();
        for (const auto& [le, count] : snap.buckets) {
            fmt::format_to(std::back_inserter(out), "{}_bucket{{le=\"{:.3f}\"}} {}\n", name, le, count);
        }
        fmt::format_to(std::back_inserter(out), "{}_bucket{{le=\"+Inf\"}} {}\n", name, snap.count);
        fmt::format_to(std::back_inserter(out), "{}_sum {:.6f}\n", name, snap.sum);
        fmt::format_to(std::back_inserter(out), "{}_count {}\n", name, snap.count);
    }

    return out;
}

} // namespace Core::Telemetry
