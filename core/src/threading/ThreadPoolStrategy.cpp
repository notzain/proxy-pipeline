#include "core/threading/ThreadPoolStrategy.hpp"
#include "core/handler/IHandler.hpp"
#include "core/message/MessageView.hpp"
#include "core/message/UdpMetadata.hpp"
#include "core/telemetry/MetricsRegistry.hpp"
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

namespace {

    std::size_t computeFlowWorkerIndex(
        std::size_t channelIdx,
        std::span<const std::byte> rawPacket,
        std::size_t numWorkers) noexcept
    {
        if (numWorkers <= 1) {
            return 0;
        }
        auto optView = Core::Message::MessageView::from(rawPacket);
        if (optView && optView->protocol() == Core::Message::ProtocolId::Udp) {
            const auto* meta = optView->getMetadata<Core::Message::UdpMetadata>();
            if (meta && meta->srcPort != 0) {
                // FNV-1a style mix of channel index + client IP + client port to pin flow to a consistent worker
                std::uint64_t hash = 14695981039346656037ULL ^ channelIdx;
                hash = (hash ^ meta->srcPort) * 1099511628211ULL;
                if (meta->addressFamily == 6) {
                    for (std::uint8_t b : meta->srcIpv6) {
                        hash = (hash ^ b) * 1099511628211ULL;
                    }
                } else {
                    hash = (hash ^ meta->srcIp) * 1099511628211ULL;
                }
                return static_cast<std::size_t>(hash % numWorkers);
            }
        }
        return channelIdx % numWorkers;
    }

    /**
     * @brief Drains tasks from an SPSC worker queue, grouping by channel for batched handler invocation.
     */
    void drainWorkerTasks(
        Handler::IHandler& handler,
        std::span<const MessageTask> tasks,
        std::size_t& invalidEnvelopeCount)
    {
        static auto& s_msgCounter = Telemetry::MetricsRegistry::instance().counter("messages_ingress_total", "Total messages received at ingress");
        static auto& s_bytesCounter = Telemetry::MetricsRegistry::instance().counter("bytes_ingress_total", "Total bytes received at ingress");
        static auto& s_droppedCounter = Telemetry::MetricsRegistry::instance().counter("messages_dropped_total", "Total malformed messages dropped");

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
            std::size_t batchBytes = 0;
            std::size_t spanCount = batchEnd - batchStart;
            for (std::size_t k = 0; k < spanCount; ++k) {
                auto payloadSpan = tasks[batchStart + k].payload();
                auto optView = Core::Message::MessageView::from(payloadSpan);
                if (optView) {
                    views[validViews++] = *optView;
                    batchBytes += payloadSpan.size();
                } else {
                    ++invalidEnvelopeCount;
                    s_droppedCounter.increment();
                    if (invalidEnvelopeCount <= 8 || (invalidEnvelopeCount & 1023) == 0) {
                        Proxy::Log::warn("[ThreadPoolStrategy] Channel '{}' task received invalid envelope ({} bytes, total dropped: {}).",
                            chName, payloadSpan.size(), invalidEnvelopeCount);
                    }
                }
            }
            if (validViews > 0) {
                static auto& s_latencyHist = Telemetry::MetricsRegistry::instance().latencyHistogram(
                    "message_processing_duration_us", "Message transit and processing duration in microseconds");

                uint64_t nowNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                        .count());

                for (std::size_t i = 0; i < validViews; ++i) {
                    const auto& msg = views[i];
                    if (msg.timestampNs() > 0 && nowNs >= msg.timestampNs()) {
                        double durationUs = static_cast<double>(nowNs - msg.timestampNs()) / 1000.0;
                        s_latencyHist.observe(durationUs);
                    }
                }

                s_msgCounter.increment(validViews);
                s_bytesCounter.increment(batchBytes);
                handler.processBatch(chName, std::span<const Core::Message::MessageView>(views.data(), validViews));
            }
            batchStart = batchEnd;
        }
    }

    using QueueType = Core::Queue::SpscQueue<MessageTask, 1024>;
    using SharedQueueType = Core::Queue::MpmcQueue<MessageTask, 4096>;

    /**
     * @brief Ingests datagrams directly into worker queue slot memory (zero-copy) for maximum throughput.
     */
    void pollUnconstrained(
        std::string_view channelName,
        ::Backend::IInputChannel& channel,
        SharedQueueType* sharedQueue,
        std::span<const std::unique_ptr<QueueType>> queues,
        std::atomic<std::size_t>& nextWorker,
        const std::shared_ptr<Memory::BufferPool>& bufferPool,
        Handler::IHandler::Timeout timeout,
        std::stop_token stoken)
    {
        if (sharedQueue) {
            constexpr std::size_t maxBatch = 16;
            auto batchBuffers = std::make_unique<std::array<std::array<std::byte, 65535>, maxBatch>>();
            std::array<::Backend::BufferSlot, maxBatch> batchSlots;
            for (std::size_t i = 0; i < maxBatch; ++i) {
                batchSlots[i] = ::Backend::BufferSlot { std::span<std::byte>((*batchBuffers)[i].data(), (*batchBuffers)[i].size()), 0 };
            }

            const std::size_t received = channel.receiveBatch(
                std::span<::Backend::BufferSlot>(batchSlots.data(), maxBatch),
                timeout);

            for (std::size_t i = 0; i < received; ++i) {
                MessageTask task;
                const std::size_t msgSize = batchSlots[i].size;
                if (msgSize > MessageTask::SmallBufferSize && bufferPool) {
                    if (auto slice = bufferPool->acquire()) {
                        std::memcpy(slice->data(), (*batchBuffers)[i].data(), msgSize);
                        task.assignSlice(channelName, std::move(*slice), msgSize);
                    } else {
                        task.assign(channelName, std::span<const std::byte>((*batchBuffers)[i].data(), msgSize));
                    }
                } else {
                    task.assign(channelName, std::span<const std::byte>((*batchBuffers)[i].data(), msgSize));
                }

                if (!sharedQueue->tryPush(std::move(task))) {
                    sharedQueue->push(std::move(task), stoken);
                }
            }
            return;
        }

        constexpr std::size_t maxBatch = 16;
        bool hasOversized = false;

        auto fillSlots = [&](std::span<MessageTask> slots) -> std::size_t {
            std::array<::Backend::BufferSlot, maxBatch> inPlaceSlots;
            const std::size_t n = std::min(slots.size(), maxBatch);
            for (std::size_t i = 0; i < n; ++i) {
                inPlaceSlots[i] = ::Backend::BufferSlot { slots[i].prepareInline(channelName), 0 };
            }
            const std::size_t nReceived = channel.receiveBatch(
                std::span<::Backend::BufferSlot>(inPlaceSlots.data(), n),
                timeout);
            for (std::size_t i = 0; i < nReceived; ++i) {
                slots[i].commitInline(inPlaceSlots[i].size);
                if (inPlaceSlots[i].size == MessageTask::SmallBufferSize) {
                    hasOversized = true;
                }
            }
            return nReceived;
        };

        const std::size_t numWorkers = queues.size();
        const std::size_t start = nextWorker.fetch_add(1, std::memory_order_relaxed);
        QueueType* targetQueue = nullptr;
        for (std::size_t attempt = 0; attempt < numWorkers; ++attempt) {
            auto& q = *queues[(start + attempt) % numWorkers];
            if (auto res = q.tryPushInPlace(maxBatch, fillSlots, stoken)) {
                targetQueue = &q;
                break;
            }
        }
        if (!targetQueue) {
            targetQueue = queues[start % numWorkers].get();
            targetQueue->pushInPlace(maxBatch, fillSlots, stoken);
        }

        if (hasOversized && targetQueue) {
            std::array<std::byte, 65535> jumboBuf;
            constexpr Handler::IHandler::Timeout zeroTimeout { 0 };
            std::size_t jumboBytes = channel.receiveInto(jumboBuf, zeroTimeout);
            if (jumboBytes > 0) {
                targetQueue->pushBatch(
                    1,
                    [&](MessageTask& slot, std::size_t) {
                        if (jumboBytes > MessageTask::SmallBufferSize && bufferPool) {
                            if (auto slice = bufferPool->acquire()) {
                                std::memcpy(slice->data(), jumboBuf.data(), jumboBytes);
                                slot.assignSlice(channelName, std::move(*slice), jumboBytes);
                                return;
                            }
                        }
                        slot.assign(channelName, std::span<const std::byte>(jumboBuf.data(), jumboBytes));
                    },
                    stoken);
            }
        }
    }

    /**
     * @brief Ingests datagrams and groups them by client flow / channel to guarantee strict FIFO ordering.
     */
    void pollStrictFifo(
        std::string_view channelName,
        ::Backend::IInputChannel& channel,
        std::size_t channelIdx,
        std::span<const std::unique_ptr<QueueType>> queues,
        const std::shared_ptr<Memory::BufferPool>& bufferPool,
        Handler::IHandler::Timeout timeout,
        std::stop_token stoken)
    {
        constexpr std::size_t maxBatch = 16;

        // Sized to 16 slots x 65,535 B (~1 MB) to receive max UDP/UDS datagrams for flow inspection without truncation.
        // Sized to maxBatch x 65,535 B to receive full datagram batches without truncation.
        // Allocated on the heap to prevent thread stack overflow, while avoiding recurring allocations in the poll loop.
        auto batchBuffers = std::make_unique<std::array<std::array<std::byte, 65535>, maxBatch>>();
        std::array<::Backend::BufferSlot, maxBatch> batchSlots;
        for (std::size_t i = 0; i < maxBatch; ++i) {
            batchSlots[i] = ::Backend::BufferSlot { std::span<std::byte>((*batchBuffers)[i].data(), (*batchBuffers)[i].size()), 0 };
        }

        const std::size_t received = channel.receiveBatch(
            std::span<::Backend::BufferSlot>(batchSlots.data(), maxBatch),
            timeout);

        if (received == 0) {
            return;
        }

        const std::size_t numWorkers = queues.size();
        std::size_t runStart = 0;
        while (runStart < received) {
            std::span<const std::byte> firstSpan((*batchBuffers)[runStart].data(), batchSlots[runStart].size);
            const std::size_t workerIdx = computeFlowWorkerIndex(channelIdx, firstSpan, numWorkers);

            std::size_t runEnd = runStart + 1;
            while (runEnd < received) {
                std::span<const std::byte> nextSpan((*batchBuffers)[runEnd].data(), batchSlots[runEnd].size);
                if (computeFlowWorkerIndex(channelIdx, nextSpan, numWorkers) != workerIdx) {
                    break;
                }
                ++runEnd;
            }

            const std::size_t runCount = runEnd - runStart;
            queues[workerIdx]->pushBatch(
                runCount,
                [&](MessageTask& slot, std::size_t idx) {
                    const std::size_t pktIdx = runStart + idx;
                    const std::size_t pktSize = batchSlots[pktIdx].size;
                    if (pktSize > MessageTask::SmallBufferSize && bufferPool) {
                        if (auto slice = bufferPool->acquire()) {
                            std::memcpy(slice->data(), (*batchBuffers)[pktIdx].data(), pktSize);
                            slot.assignSlice(channelName, std::move(*slice), pktSize);
                            return;
                        }
                    }
                    slot.assign(channelName, std::span<const std::byte>((*batchBuffers)[pktIdx].data(), pktSize));
                },
                stoken);

            runStart = runEnd;
        }
    }

} // namespace

ThreadPoolStrategy::ThreadPoolStrategy(std::size_t numWorkerThreads, bool strictFifo)
    : m_numWorkerThreads(numWorkerThreads)
    , m_strictFifo(strictFifo)
{
    if (m_numWorkerThreads == 0) {
        m_numWorkerThreads = std::max(2UL, static_cast<std::size_t>(std::thread::hardware_concurrency()));
    }

    m_workerQueues.reserve(m_numWorkerThreads);
    for (std::size_t i = 0; i < m_numWorkerThreads; ++i) {
        m_workerQueues.emplace_back(std::make_unique<QueueType>());
    }

    if (!m_strictFifo) {
        m_sharedQueue = std::make_unique<SharedQueueType>();
    }
    m_bufferPool = std::make_shared<Memory::BufferPool>(65536, 256);
}

void ThreadPoolStrategy::setStrictFifo(bool enable) noexcept
{
    m_strictFifo = enable;
    if (!enable && !m_sharedQueue) {
        m_sharedQueue = std::make_unique<SharedQueueType>();
    }
}

bool ThreadPoolStrategy::isStrictFifo() const noexcept
{
    return m_strictFifo;
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

    const auto channelCount = handler.inputChannelCount();
    Proxy::Log::info("[ThreadPoolStrategy] Starting with {} pool workers and {} channel poller(s)...",
        m_numWorkerThreads, channelCount);

    // 1. Launch pool worker threads with partitioned queues
    for (std::size_t i = 0; i < m_numWorkerThreads; ++i) {
        m_workerThreads.emplace_back([this, &handler, i](std::stop_token stoken) {
            auto& myQueue = *m_workerQueues[i];
            std::size_t invalidEnvelopeCount = 0;

            if (m_sharedQueue) {
                std::array<MessageTask, 16> batchTasks;
                while (!stoken.stop_requested()) {
                    std::size_t loaded = 0;
                    std::size_t count = m_sharedQueue->popBatch(16, [&](MessageTask&& task) {
                        batchTasks[loaded++] = std::move(task);
                    });
                    if (count > 0) {
                        drainWorkerTasks(handler, std::span<const MessageTask>(batchTasks.data(), count), invalidEnvelopeCount);
                    } else {
                        std::this_thread::yield();
                    }
                }
                return;
            }

            while (!stoken.stop_requested()) {
                myQueue.drainContiguous(16, [&handler, &invalidEnvelopeCount](std::span<const MessageTask> tasks) { drainWorkerTasks(handler, tasks, invalidEnvelopeCount); }, stoken);
            }
        });
    }

    // 2. Launch channel poller threads to enqueue message tasks
    const bool enforceFifo = m_strictFifo;
    std::size_t channelIdx = 0;
    handler.forEachInputChannel([this, channelTimeout, enforceFifo, &channelIdx](::Backend::IInputChannel& channel) {
        auto* channelPtr = &channel;
        const auto currentChannelIdx = channelIdx++;
        m_channelThreads.emplace_back([this, channelName = channel.name(), channelPtr, currentChannelIdx, channelTimeout, enforceFifo](std::stop_token stoken) {
            while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
                if (!enforceFifo) {
                    pollUnconstrained(channelName, *channelPtr, m_sharedQueue.get(), m_workerQueues, m_nextWorker, m_bufferPool, channelTimeout, stoken);
                } else {
                    pollStrictFifo(channelName, *channelPtr, currentChannelIdx, m_workerQueues, m_bufferPool, channelTimeout, stoken);
                }
            }
        });
    });

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

    if (m_sharedQueue) {
        m_sharedQueue->stop();
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
