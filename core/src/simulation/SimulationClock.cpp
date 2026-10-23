#include "core/simulation/SimulationClock.hpp"

namespace Core::Simulation {

SimulationClock::SimulationClock(Mode mode, std::uint64_t startTimestampNs)
    : m_mode(mode)
    , m_virtualNowNs(startTimestampNs)
    , m_realtimeStart(std::chrono::steady_clock::now())
    , m_realtimeBaseNs(startTimestampNs)
{
}

SimulationClock::~SimulationClock() = default;

std::uint64_t SimulationClock::nowNs() const noexcept
{
    if (m_mode == Mode::Virtual) {
        return m_virtualNowNs;
    }

    auto elapsed = std::chrono::steady_clock::now() - m_realtimeStart;
    auto elapsedNs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
    return m_realtimeBaseNs + elapsedNs;
}

void SimulationClock::advance(std::chrono::nanoseconds duration) noexcept
{
    if (m_mode == Mode::Virtual) {
        m_virtualNowNs += static_cast<std::uint64_t>(duration.count());
    }
}

void SimulationClock::setTimeNs(std::uint64_t timestampNs) noexcept
{
    if (m_mode == Mode::Virtual) {
        m_virtualNowNs = timestampNs;
    } else {
        m_realtimeBaseNs = timestampNs;
        m_realtimeStart = std::chrono::steady_clock::now();
    }
}

void SimulationClock::setMode(Mode mode) noexcept
{
    if (m_mode == mode) {
        return;
    }
    if (mode == Mode::Realtime) {
        m_realtimeBaseNs = m_virtualNowNs;
        m_realtimeStart = std::chrono::steady_clock::now();
    }
    m_mode = mode;
}

SimulationClock::Mode SimulationClock::mode() const noexcept
{
    return m_mode;
}

void SimulationClock::reset(std::uint64_t startTimestampNs) noexcept
{
    m_virtualNowNs = startTimestampNs;
    m_realtimeBaseNs = startTimestampNs;
    m_realtimeStart = std::chrono::steady_clock::now();
}

} // namespace Core::Simulation

