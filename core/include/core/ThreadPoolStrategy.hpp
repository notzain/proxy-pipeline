#pragma once

#include "core/IExecutionStrategy.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <span>
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

    struct MessageTask {
        static constexpr std::size_t SmallBufferSize = 256;

        std::string channelName;
        std::array<std::byte, SmallBufferSize> inlineBuffer;
        std::size_t inlineSize { 0 };
        std::vector<std::byte> heapBuffer;

        MessageTask(std::string name, std::span<const std::byte> data);
        MessageTask(MessageTask&& other) noexcept;
        MessageTask& operator=(MessageTask&& other) noexcept;
        MessageTask(const MessageTask&) = delete;
        MessageTask& operator=(const MessageTask&) = delete;
        ~MessageTask();

        [[nodiscard]] std::span<const std::byte> payload() const noexcept;
    };

    std::queue<MessageTask> m_taskQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;

    std::vector<std::jthread> m_channelThreads;
    std::vector<std::jthread> m_workerThreads;
    std::jthread m_stepThread;
};

} // namespace Core
