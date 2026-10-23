#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace Core::Telemetry {

/**
 * @brief Lock-free atomic gauge representing a value that can arbitrarily go up and down.
 */
class Gauge {
public:
    explicit Gauge(std::string name = "", std::string help = "");
    ~Gauge();

    Gauge(const Gauge&) = delete;
    Gauge& operator=(const Gauge&) = delete;

    Gauge(Gauge&& other) noexcept;
    Gauge& operator=(Gauge&& other) noexcept;

    void set(int64_t val) noexcept;
    void add(int64_t delta) noexcept;
    void subtract(int64_t delta) noexcept;

    [[nodiscard]] int64_t value() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const std::string& help() const noexcept;

private:
    std::string m_name;
    std::string m_help;
    alignas(64) std::atomic<int64_t> m_value { 0 };
};

} // namespace Core::Telemetry
