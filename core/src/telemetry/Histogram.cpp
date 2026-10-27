#include "core/telemetry/Histogram.hpp"
#include "common/compat/BitCast.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace Core::Telemetry {

namespace {

    inline void atomicAddDouble(std::atomic<uint64_t>& atomicBits, double delta) noexcept
    {
        uint64_t currentBits = atomicBits.load(std::memory_order_relaxed);
        while (true) {
            double currentVal = Common::Compat::bit_cast<double>(currentBits);
            double newVal = currentVal + delta;
            uint64_t newBits = Common::Compat::bit_cast<uint64_t>(newVal);
            if (atomicBits.compare_exchange_weak(currentBits, newBits, std::memory_order_relaxed, std::memory_order_relaxed)) {
                break;
            }
        }
    }

    constexpr double DefaultLatencyBounds[] = {
        1.0, 5.0, 10.0, 25.0, 50.0, 100.0, 250.0, 500.0,
        1000.0, 2500.0, 5000.0, 10000.0, 25000.0, 50000.0, 100000.0
    };

} // namespace

Histogram::Histogram() = default;

Histogram::Histogram(std::string name, std::string help, Common::Compat::span<const double> bucketBounds)
    : m_name(std::move(name))
    , m_help(std::move(help))
    , m_bounds(bucketBounds.begin(), bucketBounds.end())
{
    std::sort(m_bounds.begin(), m_bounds.end());
    m_buckets.reserve(m_bounds.size());
    for (double bound : m_bounds) {
        m_buckets.emplace_back(bound);
    }
}

Histogram Histogram::defaultLatency(std::string name, std::string help)
{
    return Histogram(std::move(name), std::move(help), DefaultLatencyBounds);
}

Histogram::~Histogram() = default;

Histogram::Histogram(Histogram&& other) noexcept
    : m_name(std::move(other.m_name))
    , m_help(std::move(other.m_help))
    , m_bounds(std::move(other.m_bounds))
    , m_buckets(std::move(other.m_buckets))
    , m_count(other.m_count.load(std::memory_order_relaxed))
    , m_sumRawBits(other.m_sumRawBits.load(std::memory_order_relaxed))
{
}

Histogram& Histogram::operator=(Histogram&& other) noexcept
{
    if (this != &other) {
        m_name = std::move(other.m_name);
        m_help = std::move(other.m_help);
        m_bounds = std::move(other.m_bounds);
        m_buckets = std::move(other.m_buckets);
        m_count.store(other.m_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_sumRawBits.store(other.m_sumRawBits.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
}

void Histogram::observe(double value) noexcept
{
    m_count.fetch_add(1, std::memory_order_relaxed);
    atomicAddDouble(m_sumRawBits, value);

    // Cumulative histogram: increment all buckets where bound >= value
    for (auto& bucket : m_buckets) {
        if (value <= bucket.upperBond) {
            bucket.count.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

double Histogram::sum() const noexcept
{
    uint64_t bits = m_sumRawBits.load(std::memory_order_relaxed);
    return Common::Compat::bit_cast<double>(bits);
}

uint64_t Histogram::count() const noexcept
{
    return m_count.load(std::memory_order_relaxed);
}

const std::string& Histogram::name() const noexcept
{
    return m_name;
}

const std::string& Histogram::help() const noexcept
{
    return m_help;
}

Histogram::Snapshot Histogram::snapshot() const
{
    Snapshot snap;
    snap.count = m_count.load(std::memory_order_relaxed);
    snap.sum = sum();
    snap.buckets.reserve(m_buckets.size());

    for (const auto& b : m_buckets) {
        snap.buckets.emplace_back(b.upperBond, b.count.load(std::memory_order_relaxed));
    }
    return snap;
}

} // namespace Core::Telemetry
