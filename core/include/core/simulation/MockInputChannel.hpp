#pragma once

#include "backend/IInputChannel.hpp"
#include "common/compat/Span.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace Core::Simulation {

using ::Backend::BatchResult;
using ::Backend::BytesResult;

class SimulationClock;

/**
 * @brief Represents a scheduled envelope or payload inside MockInputChannel.
 */
struct TimedMessage {
    std::uint64_t scheduledTimeNs { 0 }; ///< Virtual timestamp when message becomes available (0 = immediately)
    std::vector<std::byte> data; ///< Serialized MessageEnvelope or raw payload
};

/**
 * @brief Mock implementation of Backend::IInputChannel for deterministic simulation and replays.
 *
 * Implements receive(), receiveInto(), and receiveBatch() with support for:
 *  - Deterministic Virtual Clock integration (only returns messages where scheduledTimeNs <= clock.nowNs())
 *  - Queue preloading from memory, vectors, or PCAP streams
 *  - Vectorized batch draining
 */
class MockInputChannel : public ::Backend::IInputChannel {
public:
    explicit MockInputChannel(std::string name = "", const SimulationClock* clock = nullptr);
    explicit MockInputChannel(const SimulationClock* clock);
    ~MockInputChannel() override;

    /**
     * @brief Pushes a message into the mock queue.
     * @param data Serialized bytes.
     * @param scheduledTimeNs When the message becomes visible (0 = immediate).
     */
    void pushMessage(Common::Compat::span<const std::byte> data, std::uint64_t scheduledTimeNs = 0);

    /**
     * @brief Pushes an ASCII/string payload into the mock queue.
     */
    void pushMessage(std::string_view text, std::uint64_t scheduledTimeNs = 0);

    /**
     * @brief Clears all queued messages.
     */
    void clear();

    /**
     * @brief Sets or updates the simulation clock reference.
     */
    void setClock(const SimulationClock* clock) noexcept;

    /**
     * @brief Returns number of pending messages in the mock channel.
     */
    [[nodiscard]] std::size_t pendingCount() const;

    // Backend::IInputChannel interface implementation
    ::Backend::BytesResult receiveInto(Common::Compat::span<std::byte> destination, Timeout timeout) override;
    ::Backend::BatchResult receiveBatch(
        Common::Compat::span<::Backend::BufferSlot> slots,
        Timeout timeout) override;

private:
    [[nodiscard]] bool isReady(const TimedMessage& msg) const;

    const SimulationClock* m_clock { nullptr };
    mutable std::mutex m_mutex;
    std::deque<TimedMessage> m_queue;
};

} // namespace Core::Simulation
