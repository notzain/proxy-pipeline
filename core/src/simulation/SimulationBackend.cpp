#include "core/simulation/SimulationBackend.hpp"
#include <type_traits>
#include <variant>

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
    auto ch = std::make_unique<MockInputChannel>(std::string(name), m_clock);
    m_inputMap[std::string(name)] = ch.get();
    return ch;
}

std::unique_ptr<::Backend::IOutputChannel> SimulationBackend::createOutputChannel(
    std::string_view name,
    const ::Backend::IConfig&)
{
    auto ch = std::make_unique<MockOutputChannel>(std::string(name), m_clock);
    m_outputMap[std::string(name)] = ch.get();
    return ch;
}

bool SimulationBackend::tracksMessageResults() const noexcept
{
    return true;
}

void SimulationBackend::onMessageResult(
    std::string_view inputChannel,
    Common::Compat::span<const std::byte>,
    const ::Backend::MessageResult& result)
{
    RecordedMessageResult entry;
    entry.inputChannel = std::string(inputChannel);
    entry.recordedTimeNs = m_clock ? m_clock->nowNs() : 0;

    result.match(
        [&](const ::Backend::Forwarded& outcome) {
            entry.resultType = "Forwarded";
            entry.outputChannel = std::string(outcome.channel);
        },
        [&](const ::Backend::Consumed&) {
            entry.resultType = "Consumed";
        },
        [&](const ::Backend::Dropped& outcome) {
            entry.resultType = "Dropped";
            entry.dropReason = std::string(outcome.reason);
            entry.errorCode = outcome.errorCode;
        });

    std::lock_guard lock(m_resultsMutex);
    m_recordedResults.push_back(std::move(entry));
}

std::vector<RecordedMessageResult> SimulationBackend::getRecordedResults() const
{
    std::lock_guard lock(m_resultsMutex);
    return m_recordedResults;
}

Common::Memory::ObserverPtr<MockOutputChannel> SimulationBackend::getOutputChannel(std::string_view name) const
{
    auto it = m_outputMap.find(std::string(name));
    if (it != m_outputMap.end()) {
        return it->second;
    }
    return nullptr;
}

Common::Memory::ObserverPtr<MockInputChannel> SimulationBackend::getInputChannel(std::string_view name) const
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
