#pragma once

#include "backend/IBackend.hpp"
#include "core/simulation/MockInputChannel.hpp"
#include "core/simulation/MockOutputChannel.hpp"
#include "core/simulation/SimulationClock.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace Core::Simulation {

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

    /**
     * @brief Gets non-owning pointer to an active mock output channel by name.
     */
    [[nodiscard]] MockOutputChannel* getOutputChannel(std::string_view name) const;

    /**
     * @brief Gets non-owning pointer to an active mock input channel by name.
     */
    [[nodiscard]] MockInputChannel* getInputChannel(std::string_view name) const;

    /**
     * @brief Sets or updates the simulation clock reference across all managed mock channels.
     */
    void setClock(const SimulationClock* clock) noexcept;

private:
    const SimulationClock* m_clock { nullptr };
    mutable std::unordered_map<std::string, MockInputChannel*> m_inputMap;
    mutable std::unordered_map<std::string, MockOutputChannel*> m_outputMap;
};

} // namespace Core::Simulation
