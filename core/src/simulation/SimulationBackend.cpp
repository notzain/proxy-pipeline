#include "core/simulation/SimulationBackend.hpp"

namespace Core::Simulation {

SimulationBackend::SimulationBackend(const SimulationClock* clock)
    : m_clock(clock)
{
}

SimulationBackend::~SimulationBackend() = default;

void SimulationBackend::configure(const ::Backend::IConfig&)
{
}

std::unique_ptr<::Backend::IInputChannel> SimulationBackend::createInputChannel(
    std::string_view name,
    const ::Backend::IConfig&)
{
    auto ch = std::make_unique<MockInputChannel>(m_clock);
    m_inputMap[std::string(name)] = ch.get();
    return ch;
}

std::unique_ptr<::Backend::IOutputChannel> SimulationBackend::createOutputChannel(
    std::string_view name,
    const ::Backend::IConfig&)
{
    auto ch = std::make_unique<MockOutputChannel>(m_clock);
    m_outputMap[std::string(name)] = ch.get();
    return ch;
}

MockOutputChannel* SimulationBackend::getOutputChannel(std::string_view name) const
{
    auto it = m_outputMap.find(std::string(name));
    if (it != m_outputMap.end()) {
        return it->second;
    }
    return nullptr;
}

MockInputChannel* SimulationBackend::getInputChannel(std::string_view name) const
{
    auto it = m_inputMap.find(std::string(name));
    if (it != m_inputMap.end()) {
        return it->second;
    }
    return nullptr;
}

void SimulationBackend::setClock(const SimulationClock* clock) noexcept
{
    m_clock = clock;
    for (auto& [_, in] : m_inputMap) {
        if (in) {
            in->setClock(clock);
        }
    }
    for (auto& [_, out] : m_outputMap) {
        if (out) {
            out->setClock(clock);
        }
    }
}

} // namespace Core::Simulation
