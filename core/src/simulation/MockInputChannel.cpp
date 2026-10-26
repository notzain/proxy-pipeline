#include "core/simulation/MockInputChannel.hpp"
#include "core/simulation/SimulationClock.hpp"
#include <algorithm>
#include <cstring>
#include <thread>

namespace Core::Simulation {

MockInputChannel::MockInputChannel(const SimulationClock* clock)
    : m_clock(clock)
{
}

MockInputChannel::~MockInputChannel() = default;

void MockInputChannel::pushMessage(std::span<const std::byte> data, std::uint64_t scheduledTimeNs)
{
    std::lock_guard lock(m_mutex);
    TimedMessage msg;
    msg.scheduledTimeNs = scheduledTimeNs;
    msg.data.assign(data.begin(), data.end());
    m_queue.push_back(std::move(msg));
}

void MockInputChannel::pushMessage(std::string_view text, std::uint64_t scheduledTimeNs)
{
    std::span<const std::byte> dataSpan(reinterpret_cast<const std::byte*>(text.data()), text.size());
    pushMessage(dataSpan, scheduledTimeNs);
}

void MockInputChannel::clear()
{
    std::lock_guard lock(m_mutex);
    m_queue.clear();
}

void MockInputChannel::setClock(const SimulationClock* clock) noexcept
{
    m_clock = clock;
}

std::size_t MockInputChannel::pendingCount() const
{
    std::lock_guard lock(m_mutex);
    return m_queue.size();
}

bool MockInputChannel::isReady(const TimedMessage& msg) const
{
    if (msg.scheduledTimeNs == 0 || !m_clock) {
        return true;
    }
    return m_clock->nowNs() >= msg.scheduledTimeNs;
}

std::size_t MockInputChannel::receiveInto(std::span<std::byte> destination, Timeout timeout)
{
    auto startTime = std::chrono::steady_clock::now();

    while (true) {
        {
            std::lock_guard lock(m_mutex);
            if (!m_queue.empty() && isReady(m_queue.front())) {
                const auto& data = m_queue.front().data;
                std::size_t copyLen = std::min(destination.size(), data.size());
                std::memcpy(destination.data(), data.data(), copyLen);
                m_queue.pop_front();
                return copyLen;
            }
        }

        if (timeout.count() <= 0) {
            break;
        }

        auto elapsed = std::chrono::duration_cast<Timeout>(std::chrono::steady_clock::now() - startTime);
        if (elapsed >= timeout) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    return 0;
}

std::size_t MockInputChannel::receiveBatch(
    std::span<const std::span<std::byte>> destinations,
    std::span<std::size_t> outSizes,
    Timeout timeout)
{
    if (destinations.empty() || outSizes.size() < destinations.size()) {
        return 0;
    }

    auto startTime = std::chrono::steady_clock::now();
    std::size_t drained = 0;

    while (true) {
        {
            std::lock_guard lock(m_mutex);
            while (drained < destinations.size() && !m_queue.empty() && isReady(m_queue.front())) {
                const auto& item = m_queue.front();
                std::size_t copyLen = std::min(destinations[drained].size(), item.data.size());
                std::memcpy(destinations[drained].data(), item.data.data(), copyLen);
                outSizes[drained] = copyLen;
                drained++;
                m_queue.pop_front();
            }
        }

        if (drained > 0 || timeout.count() <= 0) {
            break;
        }

        auto elapsed = std::chrono::duration_cast<Timeout>(std::chrono::steady_clock::now() - startTime);
        if (elapsed >= timeout) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    return drained;
}

} // namespace Core::Simulation
