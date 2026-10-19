#include "core/ThreadPoolStrategy.hpp"
#include "core/IHandler.hpp"
#include "log/Log.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <queue>
#include <span>

namespace Core {

ThreadPoolStrategy::ThreadPoolStrategy(std::size_t numWorkerThreads)
    : m_numWorkerThreads(numWorkerThreads)
{
    if (m_numWorkerThreads == 0) {
        m_numWorkerThreads = std::max(2UL, static_cast<std::size_t>(std::thread::hardware_concurrency()));
    }

    m_workerQueues.reserve(m_numWorkerThreads);
    for (std::size_t i = 0; i < m_numWorkerThreads; ++i) {
        m_workerQueues.emplace_back(std::make_unique<QueueType>());
    }
}

ThreadPoolStrategy::~ThreadPoolStrategy()
{
    stop();
    wait();
}

void ThreadPoolStrategy::start(IHandler& handler)
{
    m_running.store(true, std::memory_order_release);
    constexpr IHandler::Timeout channelTimeout { 50 };

    const auto& channels = handler.getInputChannels();
    Proxy::Log::info("[ThreadPoolStrategy] Starting with {} pool workers and {} channel poller(s)...",
        m_numWorkerThreads, channels.size());

    // 1. Launch pool worker threads with partitioned queues
    for (std::size_t i = 0; i < m_numWorkerThreads; ++i) {
        m_workerThreads.emplace_back([this, &handler, i](std::stop_token stoken) {
            auto& myQueue = *m_workerQueues[i];

            while (!stoken.stop_requested()) {
                myQueue.drainContiguous(16, [&handler](std::span<const MessageTask> tasks) {
                    const std::size_t drainCount = tasks.size();
                    std::size_t batchStart = 0;
                    while (batchStart < drainCount) {
                        std::string_view chName = tasks[batchStart].channelName();
                        std::size_t batchEnd = batchStart + 1;
                        while (batchEnd < drainCount && tasks[batchEnd].channelName() == chName) {
                            ++batchEnd;
                        }
                        std::array<std::span<const std::byte>, 16> batchSpans;
                        std::size_t spanCount = batchEnd - batchStart;
                        for (std::size_t k = 0; k < spanCount; ++k) {
                            batchSpans[k] = tasks[batchStart + k].payload();
                        }
                        handler.onBatchReceived(chName, std::span<const std::span<const std::byte>>(batchSpans.data(), spanCount));
                        batchStart = batchEnd;
                    } }, stoken);
            }
        });
    }

    // 2. Launch channel poller threads to enqueue message tasks
    for (const auto& namedChannel : channels) {
        m_channelThreads.emplace_back([this, &namedChannel, channelTimeout](std::stop_token stoken) {
            auto enqueueTasks = [&stoken](QueueType& targetQueue, std::string_view chName,
                                    std::span<const std::span<const std::byte>> payloads) {
                targetQueue.pushBatch(payloads.size(), [&chName, payloads](MessageTask& slot, std::size_t idx) { slot.assign(chName, payloads[idx]); }, stoken);
            };

            std::array<std::array<std::byte, 2048>, 16> batchBuffers;
            std::array<std::span<std::byte>, 16> destSpans;
            for (std::size_t i = 0; i < 16; ++i) {
                destSpans[i] = std::span<std::byte>(batchBuffers[i].data(), batchBuffers[i].size());
            }
            std::array<std::size_t, 16> batchSizes;

            while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
                if (!namedChannel.channel) {
                    continue;
                }

                std::size_t batchCount = namedChannel.channel->receiveBatch(
                    std::span<const std::span<std::byte>>(destSpans.data(), destSpans.size()),
                    batchSizes,
                    channelTimeout);

                if (batchCount == 0) {
                    continue;
                }

                // If any slot filled the full 2048-byte buffer, check if an oversized jumbo packet was received
                // and route via receiveInto full-MTU fallback if needed
                static thread_local std::array<std::byte, 65535> jumboBuffer;
                bool hasOversized = false;
                for (std::size_t idx = 0; idx < batchCount; ++idx) {
                    if (batchSizes[idx] == batchBuffers[idx].size()) {
                        hasOversized = true;
                        break;
                    }
                }

                // Target worker queue using round-robin distribution
                const std::size_t targetWorker = m_nextWorker.fetch_add(1, std::memory_order_relaxed) % m_numWorkerThreads;
                auto& targetQueue = *m_workerQueues[targetWorker];

                std::array<std::span<const std::byte>, 16> spans;
                for (std::size_t idx = 0; idx < batchCount; ++idx) {
                    spans[idx] = std::span<const std::byte>(batchBuffers[idx].data(), batchSizes[idx]);
                }
                enqueueTasks(targetQueue, namedChannel.name, std::span<const std::span<const std::byte>>(spans.data(), batchCount));

                if (hasOversized) {
                    // Check if an oversized datagram is waiting in queue
                    constexpr IHandler::Timeout zeroTimeout { 0 };
                    std::size_t jumboBytes = namedChannel.channel->receiveInto(jumboBuffer, zeroTimeout);
                    if (jumboBytes > 0) {
                        std::array<std::span<const std::byte>, 1> jumboSpan = { std::span<const std::byte>(jumboBuffer.data(), jumboBytes) };
                        enqueueTasks(targetQueue, namedChannel.name, jumboSpan);
                    }
                }
            }
        });
    }

    // 3. Launch background periodic step() thread
    m_stepThread = std::jthread([this, &handler](std::stop_token stoken) {
        while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
            handler.step();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    Proxy::Log::info("[ThreadPoolStrategy] Thread pool active.");
}

void ThreadPoolStrategy::stop()
{
    m_running.store(false, std::memory_order_release);

    for (auto& t : m_channelThreads) {
        t.request_stop();
    }
    m_stepThread.request_stop();

    for (auto& t : m_workerThreads) {
        t.request_stop();
    }

    for (const auto& wq : m_workerQueues) {
        wq->stop();
    }
}

void ThreadPoolStrategy::wait()
{
    for (auto& t : m_channelThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
    m_channelThreads.clear();

    if (m_stepThread.joinable()) {
        m_stepThread.join();
    }

    for (auto& t : m_workerThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
    m_workerThreads.clear();
}

bool ThreadPoolStrategy::isRunning() const noexcept
{
    return m_running.load(std::memory_order_acquire);
}

} // namespace Core
