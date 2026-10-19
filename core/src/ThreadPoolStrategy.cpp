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

namespace {

    struct MessageTask {
        static constexpr std::size_t SmallBufferSize = 256;

        std::string channelName;
        std::array<std::byte, SmallBufferSize> inlineBuffer;
        std::size_t inlineSize { 0 };
        std::vector<std::byte> heapBuffer;

        MessageTask() = default;

        void assign(std::string_view name, std::span<const std::byte> data)
        {
            channelName = name;
            if (data.size() <= SmallBufferSize) {
                inlineSize = data.size();
                std::memcpy(inlineBuffer.data(), data.data(), inlineSize);
            } else {
                inlineSize = 0;
                heapBuffer.assign(data.begin(), data.end());
            }
        }

        MessageTask(std::string name, std::span<const std::byte> data)
            : channelName(std::move(name))
        {
            if (data.size() <= SmallBufferSize) {
                inlineSize = data.size();
                std::memcpy(inlineBuffer.data(), data.data(), inlineSize);
            } else {
                inlineSize = 0;
                heapBuffer.assign(data.begin(), data.end());
            }
        }

        MessageTask(MessageTask&& other) noexcept
            : channelName(std::move(other.channelName))
            , inlineSize(other.inlineSize)
            , heapBuffer(std::move(other.heapBuffer))
        {
            if (inlineSize > 0) {
                std::memcpy(inlineBuffer.data(), other.inlineBuffer.data(), inlineSize);
            }
        }

        MessageTask& operator=(MessageTask&& other) noexcept
        {
            if (this != &other) {
                channelName = std::move(other.channelName);
                inlineSize = other.inlineSize;
                heapBuffer = std::move(other.heapBuffer);
                if (inlineSize > 0) {
                    std::memcpy(inlineBuffer.data(), other.inlineBuffer.data(), inlineSize);
                }
            }
            return *this;
        }

        MessageTask(const MessageTask&) = delete;
        MessageTask& operator=(const MessageTask&) = delete;
        ~MessageTask() = default;

        [[nodiscard]] std::span<const std::byte> payload() const noexcept
        {
            if (inlineSize > 0) {
                return std::span<const std::byte>(inlineBuffer.data(), inlineSize);
            }
            return std::span<const std::byte>(heapBuffer.data(), heapBuffer.size());
        }
    };

} // namespace

struct alignas(128) ThreadPoolStrategy::WorkerQueue {
    static constexpr std::size_t Capacity = 2048;

    std::mutex mutex;
    std::condition_variable cvNotEmpty;
    std::condition_variable cvNotFull;
    std::array<MessageTask, Capacity> ring;
    std::size_t head { 0 };
    std::size_t tail { 0 };
    std::size_t count { 0 };
};

ThreadPoolStrategy::ThreadPoolStrategy(std::size_t numWorkerThreads)
    : m_numWorkerThreads(numWorkerThreads)
{
    if (m_numWorkerThreads == 0) {
        m_numWorkerThreads = std::max(2UL, static_cast<std::size_t>(std::thread::hardware_concurrency()));
    }

    m_workerQueues.reserve(m_numWorkerThreads);
    for (std::size_t i = 0; i < m_numWorkerThreads; ++i) {
        m_workerQueues.emplace_back(std::make_unique<WorkerQueue>());
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
                std::size_t drainStart = 0;
                std::size_t drainCount = 0;

                {
                    std::unique_lock lock(myQueue.mutex);
                    myQueue.cvNotEmpty.wait(lock, [&myQueue, &stoken] {
                        return stoken.stop_requested() || myQueue.count > 0;
                    });

                    if (stoken.stop_requested() && myQueue.count == 0) {
                        break;
                    }

                    // Process up to 16 contiguous tasks from ring buffer
                    drainStart = myQueue.head;
                    drainCount = std::min(myQueue.count, std::size_t { 16 });
                    // Ensure drain doesn't wrap around in single batch for direct slice processing
                    drainCount = std::min(drainCount, WorkerQueue::Capacity - drainStart);

                    myQueue.head = (myQueue.head + drainCount) % WorkerQueue::Capacity;
                    myQueue.count -= drainCount;

                    // If queue was previously full or high, notify waiting producers
                    myQueue.cvNotFull.notify_one();
                }

                for (std::size_t j = 0; j < drainCount; ++j) {
                    const auto& task = myQueue.ring[drainStart + j];
                    handler.onMessageReceived(task.channelName, task.payload());
                }
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
                    {
                        std::unique_lock lock(targetQueue.mutex);
                        targetQueue.cvNotFull.wait(lock, [&targetQueue, &stoken] {
                            return stoken.stop_requested() || (WorkerQueue::Capacity - targetQueue.count >= 1);
                        });
                        if (stoken.stop_requested()) {
                            break;
                        }
                        auto& slot = targetQueue.ring[targetQueue.tail];
                        slot.assign(namedChannel.name, std::span<const std::byte>(rxBuffer.data(), bytes));
                        targetQueue.tail = (targetQueue.tail + 1) % WorkerQueue::Capacity;
                        targetQueue.count += 1;
                    }
                    targetQueue.cvNotEmpty.notify_one();
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
                        {
                            std::unique_lock lock(targetQueue.mutex);
                            targetQueue.cvNotFull.wait(lock, [&targetQueue, batchCount, &stoken] {
                                return stoken.stop_requested() || (WorkerQueue::Capacity - targetQueue.count >= batchCount + 1);
                            });
                            if (stoken.stop_requested()) {
                                break;
                            }
                            for (std::size_t idx = 0; idx < batchCount; ++idx) {
                                auto& slot = targetQueue.ring[targetQueue.tail];
                                slot.assign(namedChannel.name, std::span<const std::byte>(batchBuffers[idx].data(), batchSizes[idx]));
                                targetQueue.tail = (targetQueue.tail + 1) % WorkerQueue::Capacity;
                            }
                            auto& slot = targetQueue.ring[targetQueue.tail];
                            slot.assign(namedChannel.name, std::span<const std::byte>(rxBuffer.data(), nextBytes));
                            targetQueue.tail = (targetQueue.tail + 1) % WorkerQueue::Capacity;
                            targetQueue.count += batchCount + 1;
                        }
                        targetQueue.cvNotEmpty.notify_one();
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

                // Push into target worker ring buffer with a single lock acquisition
                {
                    std::unique_lock lock(targetQueue.mutex);
                    targetQueue.cvNotFull.wait(lock, [&targetQueue, batchCount, &stoken] {
                        return stoken.stop_requested() || (WorkerQueue::Capacity - targetQueue.count >= batchCount);
                    });

                    if (stoken.stop_requested()) {
                        break;
                    }

                    for (std::size_t idx = 0; idx < batchCount; ++idx) {
                        auto& slot = targetQueue.ring[targetQueue.tail];
                        slot.assign(namedChannel.name, std::span<const std::byte>(batchBuffers[idx].data(), batchSizes[idx]));
                        targetQueue.tail = (targetQueue.tail + 1) % WorkerQueue::Capacity;
                    }
                    targetQueue.count += batchCount;
                }

                // Wake up the assigned worker
                targetQueue.cvNotEmpty.notify_one();
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
        std::unique_lock lock(wq->mutex);
        wq->cvNotEmpty.notify_all();
        wq->cvNotFull.notify_all();
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
