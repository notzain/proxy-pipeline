#include "BenchmarkRunner.hpp"
#include <mutex>

namespace Proxy::Benchmark {

BenchmarkReceiver::BenchmarkReceiver(::Backend::IBackend& backend, std::size_t expectedCount)
    : Core::Handler::IHandler(backend)
    , m_expectedCount(expectedCount)
{
    m_receiveTimestamps.reserve(m_expectedCount);
}

Backend::MessageResult BenchmarkReceiver::onMessageReceived(std::string_view, const Backend::MessageView& message)
{
    {
        std::lock_guard lock(m_mutex);
        m_receiveTimestamps.push_back(std::chrono::steady_clock::now());
        m_bytesReceived += message.payload().size();
    }

    if (m_receiveTimestamps.size() >= m_expectedCount) {
        m_cv.notify_all();
    }
    return Backend::Consumed { };
}

bool BenchmarkReceiver::waitForCompletion(std::chrono::milliseconds timeout, std::chrono::milliseconds idleDrainTimeout)
{
    std::unique_lock lock(m_mutex);
    auto startTime = std::chrono::steady_clock::now();

    while (m_receiveTimestamps.size() < m_expectedCount) {
        std::size_t countBefore = m_receiveTimestamps.size();
        auto waitSlice = (countBefore > 0) ? idleDrainTimeout : std::chrono::milliseconds(50);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime);

        if (elapsed >= timeout) {
            break;
        }

        m_cv.wait_for(lock, waitSlice);

        if (m_receiveTimestamps.size() >= m_expectedCount) {
            return true;
        }

        // If packets have already started arriving and no new packet arrived during idleDrainTimeout, stream has drained
        if (countBefore > 0 && m_receiveTimestamps.size() == countBefore) {
            break;
        }
    }
    return m_receiveTimestamps.size() >= m_expectedCount;
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
