#include "core/ThreadPoolStrategy.hpp"
#include "core/IHandler.hpp"
#include "log/Log.hpp"
#include <chrono>

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
        m_workerThreads.emplace_back([this](std::stop_token stoken) {
            std::vector<std::function<void()>> batch;
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

                for (auto& task : batch) {
                    if (task) {
                        task();
                    }
                }
            }
        });
    }

    // 2. Launch channel poller threads to enqueue message tasks
    for (const auto& namedChannel : channels) {
        m_channelThreads.emplace_back([this, &handler, &namedChannel, channelTimeout](std::stop_token stoken) {
            while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
                if (namedChannel.channel) {
                    if (auto msg = namedChannel.channel->receive(channelTimeout); msg.has_value()) {
                        {
                            std::unique_lock lock(m_queueMutex);
                            m_taskQueue.push([&handler, channelName = namedChannel.name, buffer = std::move(*msg)]() {
                                handler.onMessageReceived(channelName, buffer);
                            });
                        }
                        m_queueCv.notify_one();
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
