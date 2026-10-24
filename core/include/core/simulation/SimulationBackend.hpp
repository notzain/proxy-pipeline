#pragma once

#include "backend/IBackend.hpp"
#include "core/simulation/MockInputChannel.hpp"
#include "core/simulation/MockOutputChannel.hpp"
#include "core/simulation/SimulationClock.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core::Simulation {

/**
 * @brief Recorded MessageResult entry captured by SimulationBackend.
 */
struct RecordedMessageResult {
    std::string inputChannel;
    std::uint64_t recordedTimeNs { 0 };
    std::string resultType; ///< "Forwarded", "Consumed", or "Dropped"
    std::string outputChannel;
    std::string dropReason;
    int errorCode { 0 };
};

/**
 * @brief Concrete simulation backend for instantiating MockInputChannel and MockOutputChannel endpoints.
 */
class SimulationBackend : public ::Backend::IBackend {
public:
    explicit SimulationBackend(const SimulationClock* clock = nullptr);
    ~SimulationBackend() override;

    void configure(const ::Backend::IConfig& config) override;

    std::unique_ptr<::Backend::IInputChannel> createInputChannel(
        std::string_view name,
        const ::Backend::IConfig& config) override;

    std::unique_ptr<::Backend::IOutputChannel> createOutputChannel(
        std::string_view name,
        const ::Backend::IConfig& config) override;

    [[nodiscard]] bool tracksMessageResults() const noexcept override;

    void onMessageResult(
        std::string_view inputChannel,
        std::span<const std::byte> rawMessage,
        const ::Backend::MessageResult& result) override;

    /**
     * @brief Gets non-owning pointer to an active mock output channel by name.
     */
    [[nodiscard]] MockOutputChannel* getOutputChannel(std::string_view name) const;

    /**
     * @brief Gets non-owning pointer to an active mock input channel by name.
     */
    [[nodiscard]] MockInputChannel* getInputChannel(std::string_view name) const;

    /**
     * @brief Returns all MessageResult outcomes recorded by handlers using this backend.
     */
    [[nodiscard]] std::vector<RecordedMessageResult> getRecordedResults() const;

    /**
     * @brief Sets or updates the simulation clock reference across all managed mock channels.
     */
    void setClock(const SimulationClock* clock) noexcept;

private:
    const SimulationClock* m_clock { nullptr };
    mutable std::unordered_map<std::string, MockInputChannel*> m_inputMap;
    mutable std::unordered_map<std::string, MockOutputChannel*> m_outputMap;
    mutable std::mutex m_resultsMutex;
    std::vector<RecordedMessageResult> m_recordedResults;
};

} // namespace Core::Simulation
