#include "BenchmarkRunner.hpp"

namespace Proxy::Benchmark {

BenchmarkReceiver::BenchmarkReceiver(std::size_t expectedCount)
    : m_expectedCount(expectedCount)
{
    m_receiveTimestamps.reserve(m_expectedCount);
}

void BenchmarkReceiver::onMessageReceived(std::string_view, std::span<const std::byte> buffer)
{
    {
        std::lock_guard lock(m_mutex);
        m_receiveTimestamps.push_back(std::chrono::steady_clock::now());
        m_bytesReceived += buffer.size();
    }

    if (m_receiveTimestamps.size() >= m_expectedCount) {
        m_cv.notify_all();
        stop();
    }
}

bool BenchmarkReceiver::waitForCompletion(std::chrono::milliseconds timeout)
{
    std::unique_lock lock(m_mutex);
    return m_cv.wait_for(lock, timeout, [this] {
        return m_receiveTimestamps.size() >= m_expectedCount;
    });
}

const std::vector<std::chrono::steady_clock::time_point>& BenchmarkReceiver::getReceiveTimestamps() const noexcept
{
    return m_receiveTimestamps;
}

std::size_t BenchmarkReceiver::getBytesReceived() const noexcept
{
    return m_bytesReceived;
}

} // namespace Proxy::Benchmark
