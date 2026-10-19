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
                    }
                }, stoken);
            }
        });
    }

    // 2. Launch channel poller threads to enqueue message tasks
    for (const auto& namedChannel : channels) {
        m_channelThreads.emplace_back([this, &namedChannel, channelTimeout](std::stop_token stoken) {
            thread_local std::array<std::byte, 65535> rxBuffer;

            std::array<std::array<std::byte, 2048>, 16> batchBuffers;
            std::array<std::size_t, 16> batchSizes;
            std::size_t batchCount = 0;

            auto enqueueTasks = [&stoken](QueueType& targetQueue, std::string_view chName,
                                    std::span<const std::span<const std::byte>> payloads) {
                targetQueue.pushBatch(payloads.size(), [&chName, payloads](MessageTask& slot, std::size_t idx) {
                    slot.assign(chName, payloads[idx]);
                }, stoken);
            };

            while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
                if (!namedChannel.channel) {
                    continue;
                }

                batchCount = 0;

                // Initial receive with timeout to yield if idle
                std::size_t bytes = namedChannel.channel->receiveInto(rxBuffer, channelTimeout);
                if (bytes == 0) {
                    continue;
                }

                if (bytes <= batchBuffers[0].size()) {
                    std::memcpy(batchBuffers[batchCount].data(), rxBuffer.data(), bytes);
                    batchSizes[batchCount++] = bytes;
                } else {
                    const std::size_t targetWorker = m_nextWorker.fetch_add(1, std::memory_order_relaxed) % m_numWorkerThreads;
                    auto& targetQueue = *m_workerQueues[targetWorker];
                    std::array<std::span<const std::byte>, 1> singleSpan = { std::span<const std::byte>(rxBuffer.data(), bytes) };
                    enqueueTasks(targetQueue, namedChannel.name, singleSpan);
                    continue;
                }

                // Drain subsequent ready packets without waiting (timeout 0) up to batch limit of 16
                constexpr IHandler::Timeout zeroTimeout { 0 };
                while (batchCount < 16 && !stoken.stop_requested()) {
                    std::size_t nextBytes = namedChannel.channel->receiveInto(rxBuffer, zeroTimeout);
                    if (nextBytes == 0) {
                        break;
                    }
                    if (nextBytes <= batchBuffers[0].size()) {
                        std::memcpy(batchBuffers[batchCount].data(), rxBuffer.data(), nextBytes);
                        batchSizes[batchCount++] = nextBytes;
                    } else {
                        // Push current batch first, then dispatch large packet
                        const std::size_t targetWorker = m_nextWorker.fetch_add(1, std::memory_order_relaxed) % m_numWorkerThreads;
                        auto& targetQueue = *m_workerQueues[targetWorker];

                        std::array<std::span<const std::byte>, 17> combinedSpans;
                        for (std::size_t idx = 0; idx < batchCount; ++idx) {
                            combinedSpans[idx] = std::span<const std::byte>(batchBuffers[idx].data(), batchSizes[idx]);
                        }
                        combinedSpans[batchCount] = std::span<const std::byte>(rxBuffer.data(), nextBytes);
                        enqueueTasks(targetQueue, namedChannel.name, std::span<const std::span<const std::byte>>(combinedSpans.data(), batchCount + 1));

                        batchCount = 0;
                        break;
                    }
                }

                if (batchCount == 0) {
                    continue;
                }

                // Target worker queue using round-robin distribution
                const std::size_t targetWorker = m_nextWorker.fetch_add(1, std::memory_order_relaxed) % m_numWorkerThreads;
                auto& targetQueue = *m_workerQueues[targetWorker];

                std::array<std::span<const std::byte>, 16> spans;
                for (std::size_t idx = 0; idx < batchCount; ++idx) {
                    spans[idx] = std::span<const std::byte>(batchBuffers[idx].data(), batchSizes[idx]);
                }
                enqueueTasks(targetQueue, namedChannel.name, std::span<const std::span<const std::byte>>(spans.data(), batchCount));
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
