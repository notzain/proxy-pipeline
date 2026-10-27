#pragma once

#include "common/compat/Span.hpp"
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace Core::Telemetry {

/**
 * @brief Lock-free fixed-bucket latency histogram.
 *
 * Records elapsed time samples (e.g. nanoseconds or microseconds) into configured
 * cumulative buckets without acquiring locks or allocating memory.
 */
class Histogram {
public:
    struct Bucket {
        double upperBond { 0.0 };
        alignas(64) std::atomic<uint64_t> count { 0 };

        Bucket() = default;
        explicit Bucket(double bound)
            : upperBond(bound)
            , count(0)
        {
        }

        Bucket(Bucket&& other) noexcept
            : upperBond(other.upperBond)
            , count(other.count.load(std::memory_order_relaxed))
        {
        }

        Bucket& operator=(Bucket&& other) noexcept
        {
            if (this != &other) {
                upperBond = other.upperBond;
                count.store(other.count.load(std::memory_order_relaxed), std::memory_order_relaxed);
            }
            return *this;
        }

        Bucket(const Bucket&) = delete;
        Bucket& operator=(const Bucket&) = delete;
    };

    struct Snapshot {
        uint64_t count { 0 };
        double sum { 0.0 };
        std::vector<std::pair<double, uint64_t>> buckets;
    };

    Histogram();

    /**
     * @brief Constructs a histogram with explicitly defined upper bounds.
     *
     * @param name Metric name.
     * @param help Description string.
     * @param bucketBounds Monotonically increasing list of bucket boundaries.
     */
    Histogram(std::string name, std::string help, Common::Compat::span<const double> bucketBounds);

    /**
     * @brief Constructs a default latency histogram with standard microsecond bucket boundaries:
     * [1, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000, 100000] µs.
     */
    static Histogram defaultLatency(std::string name, std::string help);

    ~Histogram();

    Histogram(const Histogram&) = delete;
    Histogram& operator=(const Histogram&) = delete;

    Histogram(Histogram&& other) noexcept;
    Histogram& operator=(Histogram&& other) noexcept;

    /**
     * @brief Observes a value and updates corresponding buckets lock-free.
     *
     * @param value Metric value observed (e.g. duration in µs or seconds).
     */
    void observe(double value) noexcept;

    /**
     * @brief Captures a consistent snapshot of the histogram counters.
     */
    [[nodiscard]] Snapshot snapshot() const;

    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const std::string& help() const noexcept;
    [[nodiscard]] uint64_t count() const noexcept;
    [[nodiscard]] double sum() const noexcept;

private:
    std::string m_name;
    std::string m_help;
    std::vector<double> m_bounds;
    std::vector<Bucket> m_buckets;
    alignas(64) std::atomic<uint64_t> m_count { 0 };
    alignas(64) std::atomic<uint64_t> m_sumRawBits { 0 }; // double stored as uint64_t bitcast
};

} // namespace Core::Telemetry
