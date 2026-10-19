#pragma once

#include "core/IExecutionStrategy.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <functional>
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
        std::string channelName;
        static constexpr std::size_t SmallBufferSize = 256;
        std::array<std::byte, SmallBufferSize> inlineBuffer;
        std::size_t inlineSize { 0 };
        std::vector<std::byte> heapBuffer;

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

        [[nodiscard]] std::span<const std::byte> payload() const noexcept
        {
            if (inlineSize > 0) {
                return std::span<const std::byte>(inlineBuffer.data(), inlineSize);
            }
            return std::span<const std::byte>(heapBuffer.data(), heapBuffer.size());
        }
    };

    std::queue<MessageTask> m_taskQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;

    std::vector<std::jthread> m_channelThreads;
    std::vector<std::jthread> m_workerThreads;
    std::jthread m_stepThread;
};

} // namespace Core
