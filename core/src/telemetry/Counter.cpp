#include "core/telemetry/Counter.hpp"

namespace Core::Telemetry {

Counter::Counter(std::string name, std::string help)
    : m_name(std::move(name))
    , m_help(std::move(help))
    , m_value(0)
{
}

Counter::~Counter() = default;

Counter::Counter(Counter&& other) noexcept
    : m_name(std::move(other.m_name))
    , m_help(std::move(other.m_help))
    , m_value(other.m_value.load(std::memory_order_relaxed))
{
}

Counter& Counter::operator=(Counter&& other) noexcept
{
    if (this != &other) {
        m_name = std::move(other.m_name);
        m_help = std::move(other.m_help);
        m_value.store(other.m_value.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return *this;
}

void Counter::increment(uint64_t count) noexcept
{
    m_value.fetch_add(count, std::memory_order_relaxed);
}

uint64_t Counter::value() const noexcept
{
    return m_value.load(std::memory_order_relaxed);
}

void Counter::reset() noexcept
{
    m_value.store(0, std::memory_order_relaxed);
}

const std::string& Counter::name() const noexcept
{
    return m_name;
}

const std::string& Counter::help() const noexcept
{
    return m_help;
}

} // namespace Core::Telemetry
