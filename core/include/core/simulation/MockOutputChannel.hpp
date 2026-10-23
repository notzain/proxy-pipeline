#pragma once

#include "backend/IOutputChannel.hpp"
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

namespace Core::Simulation {

class SimulationClock;

/**
 * @brief Represents a transmitted message recorded by MockOutputChannel.
 */
struct RecordedMessage {
    std::uint64_t recordedTimeNs { 0 }; ///< Virtual/real timestamp when send() occurred
    std::vector<std::byte> data; ///< Serialized MessageEnvelope or raw payload
};

/**
 * @brief Mock implementation of Backend::IOutputChannel for recording, inspecting, and asserting pipeline egress.
 *
 * Captures all send() and sendBatch() operations in thread-safe memory buffers,
 * preserving timing from SimulationClock.
 */
class MockOutputChannel : public ::Backend::IOutputChannel {
public:
    explicit MockOutputChannel(const SimulationClock* clock = nullptr);
    ~MockOutputChannel() override;

    /**
     * @brief Backend::IOutputChannel implementation.
     */
    bool send(std::span<const std::byte> message) override;
    std::size_t sendBatch(std::span<const std::span<const std::byte>> messages) override;

    /**
     * @brief Returns a snapshot of all recorded messages.
     */
    [[nodiscard]] std::vector<RecordedMessage> getRecordedMessages() const;

    /**
     * @brief Returns the total number of messages transmitted through this channel.
     */
    [[nodiscard]] std::size_t messageCount() const;

    /**
     * @brief Clears all recorded message buffers.
     */
    void clear();

    /**
     * @brief Sets or updates the simulation clock reference.
     */
    void setClock(const SimulationClock* clock) noexcept;

    /**
     * @brief Helper to check if any recorded message contains the given substring payload.
     */
    [[nodiscard]] bool containsPayload(std::string_view substring) const;

private:
    const SimulationClock* m_clock { nullptr };
    mutable std::mutex m_mutex;
    std::vector<RecordedMessage> m_recorded;
};

} // namespace Core::Simulation
