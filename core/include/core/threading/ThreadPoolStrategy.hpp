#pragma once

#include "core/memory/BufferPool.hpp"
#include "core/queue/MpmcQueue.hpp"
#include "core/queue/SpscQueue.hpp"
#include "core/threading/IExecutionStrategy.hpp"
#include "core/threading/MessageTask.hpp"
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace Core::Threading {

/**
 * @brief Thread-pool based execution strategy.
 *
 * Dedicated polling threads read messages from input channels and enqueue message tasks
 * onto a shared work queue. A fixed-size pool of worker threads consumes and processes
 * the messages concurrently. Handler access serialization can be enabled when callbacks
 * and step() must not overlap.
 */
class ThreadPoolStrategy : public IExecutionStrategy {
public:
    /**
     * @brief Constructs ThreadPoolStrategy with a specified number of worker threads.
     *
     * @param numWorkerThreads Number of pool workers. Defaults to hardware concurrency.
     * @param strictFifo When true (default), messages from the same client flow/channel are pinned to the same worker to guarantee strict FIFO ordering. When false, batches are distributed round-robin across worker queues for unconstrained parallel throughput.
     * @param serializeHandlerAccess When true, processBatch() and step() calls are serialized.
     */
    explicit ThreadPoolStrategy(
        std::size_t numWorkerThreads = 0,
        bool strictFifo = true,
        bool serializeHandlerAccess = false);
    ~ThreadPoolStrategy() override;

    /**
     * @brief Enables or disables strict per-flow/channel FIFO ordering.
     */
    void setStrictFifo(bool enable) noexcept;

    /**
     * @brief Returns whether strict FIFO ordering is enabled.
     */
    [[nodiscard]] bool isStrictFifo() const noexcept;

    void start(Handler::IHandler& handler) override;
    void stop() override;
    void wait() override;
    [[nodiscard]] bool isRunning() const noexcept override;

private:
    std::size_t m_numWorkerThreads;
    bool m_strictFifo { true };
    const bool m_serializeHandlerAccess;
    std::mutex m_handlerMutex;
    std::atomic<bool> m_running { false };

    using QueueType = Queue::SpscQueue<MessageTask, 1024>;
    std::vector<std::unique_ptr<QueueType>> m_workerQueues;
    std::atomic<std::size_t> m_nextWorker { 0 };

    using SharedQueueType = Queue::MpmcQueue<MessageTask, 4096>;
    std::unique_ptr<SharedQueueType> m_sharedQueue;
    std::shared_ptr<Memory::BufferPool> m_bufferPool;

    std::vector<std::jthread> m_channelThreads;
    std::vector<std::jthread> m_workerThreads;
    std::jthread m_stepThread;
};

} // namespace Core::Threading
