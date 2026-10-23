#include "core/telemetry/Gauge.hpp"

namespace Core::Telemetry {

Gauge::Gauge(std::string name, std::string help)
    : m_name(std::move(name))
    , m_help(std::move(help))
    , m_value(0)
{
}

Gauge::~Gauge() = default;

Gauge::Gauge(Gauge&& other) noexcept
    : m_name(std::move(other.m_name))
    , m_help(std::move(other.m_help))
    , m_value(other.m_value.load(std::memory_order_relaxed))
{
}

Gauge& Gauge::operator=(Gauge&& other) noexcept
{
    if (this != &other) {
        m_name = std::move(other.m_name);
        m_help = std::move(other.m_help);
        m_value.store(other.m_value.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
}

void Gauge::set(int64_t val) noexcept
{
    m_value.store(val, std::memory_order_relaxed);
}

void Gauge::add(int64_t delta) noexcept
{
    m_value.fetch_add(delta, std::memory_order_relaxed);
}

void Gauge::subtract(int64_t delta) noexcept
{
    m_value.fetch_sub(delta, std::memory_order_relaxed);
}

int64_t Gauge::value() const noexcept
{
    return m_value.load(std::memory_order_relaxed);
}

const std::string& Gauge::name() const noexcept
{
    return m_name;
}

const std::string& Gauge::help() const noexcept
{
    return m_help;
}

} // namespace Core::Telemetry
