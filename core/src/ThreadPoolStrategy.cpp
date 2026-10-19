#include "core/ThreadPoolStrategy.hpp"
#include "core/IHandler.hpp"
#include "log/Log.hpp"
#include <array>
#include <chrono>
#include <span>

namespace Core {

ThreadPoolStrategy::ThreadPoolStrategy(std::size_t numWorkerThreads)
    : m_numWorkerThreads(numWorkerThreads)
{
    if (m_numWorkerThreads == 0) {
        m_numWorkerThreads = std::thread::hardware_concurrency();
        if (m_numWorkerThreads == 0) {
            m_numWorkerThreads = 2;
        }
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

    // 1. Launch pool worker threads
    for (std::size_t i = 0; i < m_numWorkerThreads; ++i) {
        m_workerThreads.emplace_back([this, &handler](std::stop_token stoken) {
            std::vector<MessageTask> batch;
            batch.reserve(16);

            while (!stoken.stop_requested()) {
                batch.clear();
                {
                    std::unique_lock lock(m_queueMutex);
                    m_queueCv.wait(lock, [this, &stoken] {
                        return stoken.stop_requested() || !m_taskQueue.empty();
                    });

                    if (stoken.stop_requested() && m_taskQueue.empty()) {
                        break;
                    }

                    // Drain up to 16 tasks per lock acquisition
                    while (!m_taskQueue.empty() && batch.size() < 16) {
                        batch.push_back(std::move(m_taskQueue.front()));
                        m_taskQueue.pop();
                    }
                }

                for (const auto& task : batch) {
                    handler.onMessageReceived(task.channelName, task.payload());
                }
            }
        });
    }

    // 2. Launch channel poller threads to enqueue message tasks
    for (const auto& namedChannel : channels) {
        m_channelThreads.emplace_back([this, &namedChannel, channelTimeout](std::stop_token stoken) {
            thread_local std::array<std::byte, 65535> rxBuffer;
            std::vector<MessageTask> ingressBatch;
            ingressBatch.reserve(16);

            while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
                if (!namedChannel.channel) {
                    continue;
                }

                // Initial receive with timeout to yield if idle
                std::size_t bytes = namedChannel.channel->receiveInto(rxBuffer, channelTimeout);
                if (bytes == 0) {
                    continue;
                }

                ingressBatch.emplace_back(namedChannel.name, std::span<const std::byte>(rxBuffer.data(), bytes));

                // Drain subsequent ready packets without waiting (timeout 0) up to batch limit of 16
                constexpr IHandler::Timeout zeroTimeout { 0 };
                while (ingressBatch.size() < 16 && !stoken.stop_requested()) {
                    std::size_t nextBytes = namedChannel.channel->receiveInto(rxBuffer, zeroTimeout);
                    if (nextBytes == 0) {
                        break;
                    }
                    ingressBatch.emplace_back(namedChannel.name, std::span<const std::byte>(rxBuffer.data(), nextBytes));
                }

                // Bulk push into task queue with a single lock acquisition
                {
                    std::unique_lock lock(m_queueMutex);
                    for (auto& task : ingressBatch) {
                        m_taskQueue.push(std::move(task));
                    }
                }
                ingressBatch.clear();

                // Wake up waiting workers
                m_queueCv.notify_all();
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

    {
        std::unique_lock lock(m_queueMutex);
        for (auto& t : m_workerThreads) {
            t.request_stop();
        }
    }
    m_queueCv.notify_all();
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
