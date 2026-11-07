#include "core/threading/ThreadPoolStrategy.hpp"
#include "IngressBatchScratch.hpp"
#include "MessageDispatcher.hpp"
#include "backend/IBackend.hpp"
#include "backend/message/MessageView.hpp"
#include "common/compat/Span.hpp"
#include "common/memory/ByteView.hpp"
#include "common/memory/ParallelArray.hpp"
#include "core/handler/IHandler.hpp"
#include "core/telemetry/MetricsRegistry.hpp"
#include "log/Log.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <utility>

namespace Core::Threading {

namespace {

    std::size_t computeFlowWorkerIndex(
        std::size_t channelIdx,
        Common::Compat::span<const std::byte> rawPacket,
        ::Backend::PayloadFormat format,
        std::size_t numWorkers) noexcept
    {
        if (numWorkers <= 1) {
            return 0;
        }
        if (format == ::Backend::PayloadFormat::Enveloped) {
            if (const auto optView = Backend::MessageView::from(rawPacket)) {
                const auto meta = optView->metadata();
                if (!meta.empty()) {
                    // FNV-1a mix of channel index + protocol + opaque metadata bytes to pin flow to a consistent worker
                    std::uint64_t hash = 14695981039346656037ULL ^ channelIdx;
                    hash = (hash ^ optView->protocol().value()) * 1099511628211ULL;
                    for (const std::byte b : meta) {
                        hash = (hash ^ static_cast<std::uint8_t>(b)) * 1099511628211ULL;
                    }
                    return static_cast<std::size_t>(hash % numWorkers);
                }
            }
        }
        return channelIdx % numWorkers;
    }

    /**
     * @brief Drains tasks from an SPSC worker queue, grouping by channel for batched handler invocation.
     */
    void drainWorkerTasks(
        Handler::IHandler& handler,
        Common::Compat::span<const MessageTask> tasks,
        std::size_t& invalidEnvelopeCount,
        std::mutex* handlerMutex)
    {
        const std::size_t drainCount = tasks.size();
        std::size_t batchStart = 0;
        Common::Memory::ParallelArray<16, Common::Compat::span<const std::byte>, ::Backend::PayloadFormat, ::Backend::Lease> batch;

        // Group consecutive tasks from the same input channel so IHandler::processBatch() runs vectorized.
        while (batchStart < drainCount) {
            std::string_view chName = tasks[batchStart].channelName();
            std::size_t batchEnd = batchStart + 1;
            while (batchEnd < drainCount && tasks[batchEnd].channelName() == chName) {
                ++batchEnd;
            }
            std::size_t spanCount = batchEnd - batchStart;
            batch.clear();
            for (std::size_t k = 0; k < spanCount; ++k) {
                batch.push_back(
                    Common::Memory::ByteView(tasks[batchStart + k].payload()),
                    tasks[batchStart + k].format(),
                    std::move(tasks[batchStart + k].lease()));
            }
            Detail::dispatchReceivedBatch(
                handler,
                chName,
                batch.span<0>(),
                batch.span<1>(),
                batch.span<2>(),
                invalidEnvelopeCount,
                handlerMutex);
            batchStart = batchEnd;
        }
    }

    using QueueType = Common::Queue::SpscQueue<MessageTask, 1024>;
    using SharedQueueType = Common::Queue::MpmcQueue<MessageTask, 4096>;

    /**
     * @brief Populates a MessageTask from a BufferSlot and optional pre-acquired BufferSlice.
     */
    void populateTaskFromSlot(
        MessageTask& task,
        std::string_view channelName,
        ::Backend::BufferSlot& slot,
        std::optional<Common::Memory::BufferSlice>& preSlice)
    {
        if (slot.isLeased()) {
            // Zero-copy external view (e.g. Vendor): transfer view + lease without touching buffers.
            task.assignLeased(channelName, slot.filledSpan(), std::move(slot.lease));
            return;
        }

        task.setLease(std::move(slot.lease));
        const std::size_t pktSize = slot.size;
        if (preSlice.has_value()) {
            // Keep > SmallBufferSize payloads in their BufferSlice (0 copies); copy <= SmallBufferSize
            // into the inline SBO slot so the 64 KB pool block is returned to the pool immediately.
            if (pktSize > MessageTask::SmallBufferSize) {
                task.assignSlice(channelName, std::move(*preSlice), pktSize);
            } else {
                task.assign(channelName, preSlice->span().subspan(0, pktSize));
            }
            preSlice.reset();
            return;
        }

        // Pool was exhausted: copy from fallback scratch buffer.
        task.assign(channelName, slot.filledSpan());
    }

    /**
     * @brief Identifies contiguous run of slots that hash to the same worker index.
     */
    std::size_t findContiguousFlowRun(
        std::size_t startIndex,
        std::size_t totalReceived,
        std::size_t channelIdx,
        Common::Compat::span<const ::Backend::BufferSlot> slots,
        std::size_t expectedWorkerIdx,
        std::size_t numWorkers) noexcept
    {
        std::size_t runEnd = startIndex + 1;
        while (runEnd < totalReceived) {
            Common::Memory::ByteView nextSpan(slots[runEnd].filledSpan());
            if (computeFlowWorkerIndex(channelIdx, nextSpan, slots[runEnd].format(), numWorkers) != expectedWorkerIdx) {
                break;
            }
            ++runEnd;
        }
        return runEnd;
    }

    bool channelFailed(std::string_view channelName, const Common::Net::ReceiveError& error)
    {
        if (error.is<Common::Net::ReceiveTimeout>()) {
            return false;
        }
        if (error.is<Common::Net::ReceiveClosed>()) {
            Proxy::Log::error("[ThreadPoolStrategy] Input channel '{}' is closed", channelName);
            return true;
        }
        {
            Proxy::Log::error("[ThreadPoolStrategy] Input channel '{}' failed: {}",
                channelName, error.message());
            return true;
        }
    }

    bool channelFailed(std::string_view channelName, const Common::Net::BatchResult& result)
    {
        return !result.isOk() && channelFailed(channelName, result.unwrapErr().cause);
    }

    /**
     * @brief Ingests datagrams into pooled BufferSlices or leased views and enqueues them onto the shared MPMC work queue.
     */
    bool pollUnconstrained(
        std::string_view channelName,
        ::Backend::IInputChannel& channel,
        SharedQueueType& sharedQueue,
        const std::shared_ptr<Common::Memory::BufferPool>& bufferPool,
        Detail::IngressBatchScratch<16>& scratch,
        Core::Telemetry::Counter& enqueueBackpressureCounter,
        Handler::IHandler::Timeout timeout,
        Common::Compat::stop_token stoken)
    {
        auto batchSlots = scratch.preparePooled(channel, bufferPool);

        auto result = channel.receiveBatch(batchSlots, timeout);
        const bool failed = channelFailed(channelName, result);
        const std::size_t received = result.isOk() ? result.unwrap() : result.unwrapErr().receivedCount;
        scratch.releaseUnusedFrom(received);
        if (failed && received == 0) {
            return false;
        }

        for (std::size_t i = 0; i < received; ++i) {
            MessageTask task;
            populateTaskFromSlot(task, channelName, batchSlots[i], scratch.slice(i));
            if (!sharedQueue.tryPush(std::move(task))) {
                enqueueBackpressureCounter.increment();
                sharedQueue.push(std::move(task), stoken);
            }
        }
        return !failed;
    }

    /**
     * @brief Ingests datagrams and groups them by client flow / channel to guarantee strict FIFO ordering.
     */
    bool pollStrictFifo(
        std::string_view channelName,
        ::Backend::IInputChannel& channel,
        std::size_t channelIdx,
        Common::Compat::span<const std::unique_ptr<QueueType>> queues,
        const std::shared_ptr<Common::Memory::BufferPool>& bufferPool,
        Detail::IngressBatchScratch<16>& scratch,
        Core::Telemetry::Counter& enqueueBackpressureCounter,
        Handler::IHandler::Timeout timeout,
        Common::Compat::stop_token stoken)
    {
        // Strict FIFO must inspect packet headers (IP/port) BEFORE choosing which worker SPSC queue to push to,
        // so we cannot write directly into a worker's SPSC ring before receiveBatch(). Instead, we pre-attach
        // BufferSlices so the kernel writes once into pool memory, then route each contiguous flow run to its worker.
        auto batchSlots = scratch.preparePooled(channel, bufferPool);

        auto result = channel.receiveBatch(batchSlots, timeout);

        const bool failed = channelFailed(channelName, result);
        const std::size_t received = result.isOk() ? result.unwrap() : result.unwrapErr().receivedCount;
        scratch.releaseUnusedFrom(received);
        if (failed && received == 0) {
            return false;
        }
        if (received == 0) {
            return !failed;
        }

        // Coalesce consecutive packets that hash to the same worker into a single pushBatch() call.
        const std::size_t numWorkers = queues.size();
        std::size_t runStart = 0;
        while (runStart < received) {
            Common::Memory::ByteView firstSpan(batchSlots[runStart].filledSpan());
            const std::size_t workerIdx = computeFlowWorkerIndex(channelIdx, firstSpan, batchSlots[runStart].format(), numWorkers);
            const std::size_t runEnd = findContiguousFlowRun(runStart, received, channelIdx, batchSlots, workerIdx, numWorkers);
            const std::size_t runCount = runEnd - runStart;

            auto populateTasks = [&](MessageTask& slot, std::size_t idx) {
                const std::size_t pktIdx = runStart + idx;
                populateTaskFromSlot(slot, channelName, batchSlots[pktIdx], scratch.slice(pktIdx));
            };
            if (!queues[workerIdx]->tryPushBatch(runCount, populateTasks)) {
                enqueueBackpressureCounter.increment();
                queues[workerIdx]->pushBatch(runCount, populateTasks, stoken);
            }

            runStart = runEnd;
        }
        return !failed;
    }

} // namespace

ThreadPoolStrategy::ThreadPoolStrategy(
    std::size_t numWorkerThreads,
    bool strictFifo,
    bool serializeHandlerAccess)
    : m_numWorkerThreads(numWorkerThreads)
    , m_strictFifo(strictFifo)
    , m_serializeHandlerAccess(serializeHandlerAccess)
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
    m_bufferPool = handler.backend().bufferPool();
    constexpr Handler::IHandler::Timeout channelTimeout { 50 };

    const auto channelCount = handler.inputChannelCount();
    auto* enqueueBackpressureCounter = &Core::Telemetry::MetricsRegistry::instance().counter(
        "queue_enqueue_backpressure_events_total",
        "Queue enqueue attempts that required the blocking path because capacity was unavailable or a producer was contending");
    Proxy::Log::info("[ThreadPoolStrategy] Starting with {} pool workers and {} channel poller(s)...",
        m_numWorkerThreads, channelCount);

    // 1. Launch pool worker threads with partitioned queues
    for (std::size_t i = 0; i < m_numWorkerThreads; ++i) {
        m_workerThreads.emplace_back([this, &handler, i](Common::Compat::stop_token stoken) {
            auto& myQueue = *m_workerQueues[i];
            std::size_t invalidEnvelopeCount = 0;

            if (m_sharedQueue) {
                std::array<MessageTask, 16> batchTasks;
                std::uint32_t idleSpins = 0;
                while (!stoken.stop_requested()) {
                    std::size_t loaded = 0;
                    std::size_t count = m_sharedQueue->popBatch(16, [&](MessageTask&& task) {
                        batchTasks[loaded++] = std::move(task);
                    });
                    if (count > 0) {
                        idleSpins = 0;
                        drainWorkerTasks(
                            handler,
                            Common::Compat::span<const MessageTask>(batchTasks.data(), count),
                            invalidEnvelopeCount,
                            m_serializeHandlerAccess ? &m_handlerMutex : nullptr);
                    } else {
                        if (++idleSpins < 64) {
                            std::this_thread::yield();
                        } else {
                            std::this_thread::sleep_for(std::chrono::microseconds(200));
                        }
                    }
                }
                return;
            }

            while (!stoken.stop_requested()) {
                myQueue.drainContiguous(
                    16,
                    [this, &handler, &invalidEnvelopeCount](Common::Compat::span<const MessageTask> tasks) {
                        drainWorkerTasks(
                            handler,
                            tasks,
                            invalidEnvelopeCount,
                            m_serializeHandlerAccess ? &m_handlerMutex : nullptr);
                    },
                    stoken);
            }
        });
    }

    // 2. Launch channel poller threads to enqueue message tasks
    const bool enforceFifo = m_strictFifo;
    std::size_t channelIdx = 0;
    handler.forEachInputChannel([this, channelTimeout, enforceFifo, &channelIdx, enqueueBackpressureCounter](::Backend::IInputChannel& channel) {
        auto* channelPtr = &channel;
        const auto currentChannelIdx = channelIdx++;
        m_channelThreads.emplace_back([this, channelName = channel.name(), channelPtr, currentChannelIdx, channelTimeout, enforceFifo, enqueueBackpressureCounter](Common::Compat::stop_token stoken) {
            Detail::IngressBatchScratch<16> scratch;

            while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
                if (!enforceFifo) {
                    if (!pollUnconstrained(channelName, *channelPtr, *m_sharedQueue, m_bufferPool, scratch, *enqueueBackpressureCounter, channelTimeout, stoken)) {
                        break;
                    }
                } else {
                    if (!pollStrictFifo(channelName, *channelPtr, currentChannelIdx, m_workerQueues, m_bufferPool, scratch, *enqueueBackpressureCounter, channelTimeout, stoken)) {
                        break;
                    }
                }
            }
        });
    });

    // 3. Launch background periodic step() thread
    m_stepThread = Common::Compat::jthread([this, &handler](Common::Compat::stop_token stoken) {
        while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
            if (m_serializeHandlerAccess) {
                std::lock_guard lock(m_handlerMutex);
                handler.step();
            } else {
                handler.step();
            }
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
