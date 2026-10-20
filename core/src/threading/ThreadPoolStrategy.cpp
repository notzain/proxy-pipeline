#include "core/threading/ThreadPoolStrategy.hpp"
#include "core/handler/IHandler.hpp"
#include "core/message/MessageView.hpp"
#include "log/Log.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <queue>
#include <span>

namespace Core::Threading {

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

void ThreadPoolStrategy::start(Handler::IHandler& handler)
{
    m_running.store(true, std::memory_order_release);
    constexpr Handler::IHandler::Timeout channelTimeout { 50 };

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
                        std::array<Core::Message::MessageView, 16> views;
                        std::size_t validViews = 0;
                        std::size_t spanCount = batchEnd - batchStart;
                        for (std::size_t k = 0; k < spanCount; ++k) {
                            auto payloadSpan = tasks[batchStart + k].payload();
                            auto optView = Core::Message::MessageView::from(payloadSpan);
                            if (optView) {
                                views[validViews++] = *optView;
                            } else {
                                Proxy::Log::warn("[ThreadPoolStrategy] Channel '{}' task received invalid envelope ({} bytes). Dropping.", chName, payloadSpan.size());
                            }
                        }
                        if (validViews > 0) {
                            handler.onBatchReceived(chName, std::span<const Core::Message::MessageView>(views.data(), validViews));
                        }
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

            constexpr std::size_t maxBatch = 16;
            static thread_local std::array<std::byte, 65535> jumboBuffer;

            while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
                if (!namedChannel.channel) {
                    continue;
                }

                bool hasOversized = false;

                // Zero-copy ingress: the channel writes datagrams straight into ring slot memory.
                auto fillSlots = [&](std::span<MessageTask> slots) -> std::size_t {
                    std::array<std::span<std::byte>, maxBatch> destSpans;
                    std::array<std::size_t, maxBatch> sizes { };
                    const std::size_t n = std::min(slots.size(), maxBatch);
                    for (std::size_t i = 0; i < n; ++i) {
                        destSpans[i] = slots[i].prepareInline(namedChannel.name);
                    }
                    const std::size_t received = namedChannel.channel->receiveBatch(
                        std::span<const std::span<std::byte>>(destSpans.data(), n),
                        std::span<std::size_t>(sizes.data(), n),
                        channelTimeout);
                    for (std::size_t i = 0; i < received; ++i) {
                        slots[i].commitInline(sizes[i]);
                        if (sizes[i] == MessageTask::SmallBufferSize) {
                            hasOversized = true;
                        }
                    }
                    return received;
                };

                // Round-robin worker selection; skip queues currently held by another poller.
                const std::size_t start = m_nextWorker.fetch_add(1, std::memory_order_relaxed);
                QueueType* targetQueue = nullptr;
                std::size_t produced = 0;
                for (std::size_t attempt = 0; attempt < m_numWorkerThreads; ++attempt) {
                    auto& q = *m_workerQueues[(start + attempt) % m_numWorkerThreads];
                    if (auto res = q.tryPushInPlace(maxBatch, fillSlots, stoken)) {
                        targetQueue = &q;
                        produced = *res;
                        break;
                    }
                }
                if (!targetQueue) {
                    targetQueue = m_workerQueues[start % m_numWorkerThreads].get();
                    produced = targetQueue->pushInPlace(maxBatch, fillSlots, stoken);
                }

                if (produced > 0 && hasOversized) {
                    // Oversized datagram: fetch the full payload via full-MTU fallback (heap path)
                    constexpr Handler::IHandler::Timeout zeroTimeout { 0 };
                    std::size_t jumboBytes = namedChannel.channel->receiveInto(jumboBuffer, zeroTimeout);
                    if (jumboBytes > 0) {
                        std::array<std::span<const std::byte>, 1> jumboSpan = { std::span<const std::byte>(jumboBuffer.data(), jumboBytes) };
                        enqueueTasks(*targetQueue, namedChannel.name, jumboSpan);
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

} // namespace Core::Threading
