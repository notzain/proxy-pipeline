#pragma once

#include <chrono>
#include <cstdint>
#include <ratio>

namespace Core::Simulation {

/**
 * @brief Deterministic virtual simulation clock.
 *
 * Allows test suites and simulations to either:
 *  1. Step time deterministically without sleeping (Virtual mode)
 *  2. Pace execution proportional to recorded inter-arrival times (Scaled/Real-time mode)
 */
class SimulationClock {
public:
    enum class Mode {
        Virtual, ///< Clock advances strictly via advance() or step() calls
        Realtime ///< Clock advances matching wall-clock elapsed time
    };

    explicit SimulationClock(Mode mode = Mode::Virtual, std::uint64_t startTimestampNs = 1'000'000'000ULL);
    ~SimulationClock();

    /**
     * @brief Returns current simulation time in nanoseconds.
     */
    [[nodiscard]] std::uint64_t nowNs() const noexcept;

    /**
     * @brief Advances simulation time in Virtual mode.
     * Has no effect in Realtime mode.
     */
    void advance(std::chrono::nanoseconds duration) noexcept;

    /**
     * @brief Sets absolute simulation time in nanoseconds (Virtual mode).
     */
    void setTimeNs(std::uint64_t timestampNs) noexcept;

    /**
     * @brief Sets clock operating mode.
     */
    void setMode(Mode mode) noexcept;

    [[nodiscard]] Mode mode() const noexcept;

    /**
     * @brief Resets clock back to default start timestamp.
     */
    void reset(std::uint64_t startTimestampNs = 1'000'000'000ULL) noexcept;

private:
    Mode m_mode { Mode::Virtual };
    std::uint64_t m_virtualNowNs { 1'000'000'000ULL };
    std::chrono::steady_clock::time_point m_realtimeStart;
    std::uint64_t m_realtimeBaseNs { 1'000'000'000ULL };
};

} // namespace Core::Simulation
