#pragma once

#include "core/IExecutionStrategy.hpp"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace Core {

/**
 * @brief Thread-pool based execution strategy.
 *
 * Dedicated polling threads read messages from input channels and enqueue message tasks
 * onto a shared work queue. A fixed-size pool of worker threads consumes and processes
 * the messages concurrently via IMessageReceiver.
 */
class ThreadPoolStrategy : public IExecutionStrategy {
public:
    /**
     * @brief Constructs ThreadPoolStrategy with a specified number of worker threads.
     *
     * @param numWorkerThreads Number of pool workers. Defaults to hardware concurrency.
     */
    explicit ThreadPoolStrategy(std::size_t numWorkerThreads = 0);
    ~ThreadPoolStrategy() override;

    void start(IHandler& handler) override;
    void stop() override;
    void wait() override;
    [[nodiscard]] bool isRunning() const noexcept override;

private:
    std::size_t m_numWorkerThreads;
    std::atomic<bool> m_running { false };

    std::queue<std::function<void()>> m_taskQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;

    std::vector<std::jthread> m_channelThreads;
    std::vector<std::jthread> m_workerThreads;
    std::jthread m_stepThread;
};

} // namespace Core

