#include "core/simulation/MockInputChannel.hpp"
#include "common/memory/ByteView.hpp"
#include "core/simulation/SimulationClock.hpp"
#include <algorithm>
#include <cstring>
#include <system_error>
#include <thread>

namespace Core::Simulation {

MockInputChannel::MockInputChannel(std::string name, const SimulationClock* clock)
    : ::Backend::IInputChannel(std::move(name))
    , m_clock(clock)
{
}

MockInputChannel::MockInputChannel(const SimulationClock* clock)
    : MockInputChannel("", clock)
{
}

MockInputChannel::~MockInputChannel() = default;

void MockInputChannel::pushMessage(Common::Compat::span<const std::byte> data, std::uint64_t scheduledTimeNs)
{
    std::lock_guard lock(m_mutex);
    TimedMessage msg;
    msg.scheduledTimeNs = scheduledTimeNs;
    msg.data.assign(data.begin(), data.end());
    m_queue.push_back(std::move(msg));
}

void MockInputChannel::pushMessage(std::string_view text, std::uint64_t scheduledTimeNs)
{
    pushMessage(Common::Memory::ByteView(text), scheduledTimeNs);
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

Backend::BytesResult MockInputChannel::receiveInto(Common::Compat::span<std::byte> destination, Timeout timeout)
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
                return BytesResult::success(copyLen);
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

    return BytesResult::failure(::Backend::receiveTimeout());
}

Backend::BatchResult MockInputChannel::receiveBatch(
    Common::Compat::span<::Backend::BufferSlot> slots,
    Timeout timeout)
{
    if (slots.empty()) {
        return BatchResult::failure({ 0, ::Backend::receiveFailure(std::make_error_code(std::errc::invalid_argument)) });
    }

    auto startTime = std::chrono::steady_clock::now();
    std::size_t drained = 0;

    while (true) {
        {
            std::lock_guard lock(m_mutex);
            while (drained < slots.size() && !m_queue.empty() && isReady(m_queue.front())) {
                const auto& item = m_queue.front();
                std::size_t copyLen = std::min(slots[drained].buffer.size(), item.data.size());
                std::memcpy(slots[drained].buffer.data(), item.data.data(), copyLen);
                slots[drained].size = copyLen;
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

    return drained > 0
        ? BatchResult::success(drained)
        : BatchResult::failure({ 0, ::Backend::receiveTimeout() });
}

} // namespace Core::Simulation
