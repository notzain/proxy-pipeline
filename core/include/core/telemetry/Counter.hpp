#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace Core::Telemetry {

/**
 * @brief Lock-free monotonically increasing 64-bit atomic counter.
 */
class Counter {
public:
    explicit Counter(std::string name = "", std::string help = "");
    ~Counter();

    Counter(const Counter&) = delete;
    Counter& operator=(const Counter&) = delete;

    Counter(Counter&& other) noexcept;
    Counter& operator=(Counter&& other) noexcept;

    void increment(uint64_t count = 1) noexcept;
    [[nodiscard]] uint64_t value() const noexcept;
    void reset() noexcept;

    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const std::string& help() const noexcept;

private:
    std::string m_name;
    std::string m_help;
    alignas(64) std::atomic<uint64_t> m_value { 0 };
};

} // namespace Core::Telemetry
