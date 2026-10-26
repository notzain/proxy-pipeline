#pragma once

#include "backend/IBackend.hpp"
#include "backend/IConfig.hpp"
#include "core/handler/IHandler.hpp"
#include "core/message/MessageView.hpp"
#include "log/Log.hpp"
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Proxy::Benchmark {

/**
 * @brief Benchmark results structure capturing throughput and latency.
 */
struct BenchmarkResult {
    std::string backendName;
    std::size_t totalMessages { 0 };
    std::size_t verifiedMessages { 0 };
    std::chrono::nanoseconds totalDuration { 0 };
    double messagesPerSecond { 0.0 };
    double averageLatencyUs { 0.0 };
    double medianLatencyUs { 0.0 };
    double p95LatencyUs { 0.0 };
    double p99LatencyUs { 0.0 };
    double throughputMBps { 0.0 };
};

/**
 * @brief Receiver handler recording timestamps of verified messages.
 */
class BenchmarkReceiver : public Core::Handler::IHandler {
public:
    BenchmarkReceiver(::Backend::IBackend& backend, std::size_t expectedCount);

    Core::Message::MessageResult onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message) override;

    bool waitForCompletion(
        std::chrono::milliseconds timeout,
        std::chrono::milliseconds idleDrainTimeout = std::chrono::milliseconds(500));

    [[nodiscard]] const std::vector<std::chrono::steady_clock::time_point>& getReceiveTimestamps() const noexcept;
    [[nodiscard]] std::size_t getBytesReceived() const noexcept;

private:
    std::size_t m_expectedCount;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<std::chrono::steady_clock::time_point> m_receiveTimestamps;
    std::size_t m_bytesReceived { 0 };
};

} // namespace Proxy::Benchmark
