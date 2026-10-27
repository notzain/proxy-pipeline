#include "core/simulation/MockOutputChannel.hpp"
#include "core/simulation/SimulationClock.hpp"
#include <chrono>
#include <string_view>

namespace Core::Simulation {

MockOutputChannel::MockOutputChannel(std::string name, const SimulationClock* clock)
    : ::Backend::IOutputChannel(std::move(name))
    , m_clock(clock)
{
}

MockOutputChannel::MockOutputChannel(const SimulationClock* clock)
    : MockOutputChannel("", clock)
{
}

MockOutputChannel::~MockOutputChannel() = default;

bool MockOutputChannel::send(Common::Compat::span<const std::byte> message)
{
    std::uint64_t now = m_clock ? m_clock->nowNs()
                                : static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                          .count());

    std::lock_guard lock(m_mutex);
    RecordedMessage rec;
    rec.recordedTimeNs = now;
    rec.data.assign(message.begin(), message.end());
    m_recorded.push_back(std::move(rec));
    return true;
}

std::size_t MockOutputChannel::sendBatch(Common::Compat::span<const Common::Compat::span<const std::byte>> messages)
{
    std::uint64_t now = m_clock ? m_clock->nowNs()
                                : static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                          .count());

    std::lock_guard lock(m_mutex);
    m_recorded.reserve(m_recorded.size() + messages.size());
    for (const auto& msg : messages) {
        RecordedMessage rec;
        rec.recordedTimeNs = now;
        rec.data.assign(msg.begin(), msg.end());
        m_recorded.push_back(std::move(rec));
    }
    return messages.size();
}

std::vector<RecordedMessage> MockOutputChannel::getRecordedMessages() const
{
    std::lock_guard lock(m_mutex);
    return m_recorded;
}

std::size_t MockOutputChannel::messageCount() const
{
    std::lock_guard lock(m_mutex);
    return m_recorded.size();
}

void MockOutputChannel::clear()
{
    std::lock_guard lock(m_mutex);
    m_recorded.clear();
}

void MockOutputChannel::setClock(const SimulationClock* clock) noexcept
{
    m_clock = clock;
}

bool MockOutputChannel::containsPayload(std::string_view substring) const
{
    std::lock_guard lock(m_mutex);
    for (const auto& rec : m_recorded) {
        std::string_view str(reinterpret_cast<const char*>(rec.data.data()), rec.data.size());
        if (str.find(substring) != std::string_view::npos) {
            return true;
        }
    }
    return false;
}

} // namespace Core::Simulation
