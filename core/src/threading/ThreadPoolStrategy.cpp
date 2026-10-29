#include "core/threading/ThreadPoolStrategy.hpp"
#include "MessageDispatcher.hpp"
#include "backend/MessageView.hpp"
#include "backend/UdpMetadata.hpp"
#include "common/compat/Span.hpp"
#include "common/memory/ByteView.hpp"
#include "common/memory/MutableByteView.hpp"
#include "core/handler/IHandler.hpp"
#include "log/Log.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <utility>

namespace Core::Threading {

namespace {

    std::size_t computeFlowWorkerIndex(
        std::size_t channelIdx,
        Common::Compat::span<const std::byte> rawPacket,
        std::size_t numWorkers) noexcept
    {
        if (numWorkers <= 1) {
            return 0;
        }
        auto optView = Backend::MessageView::from(rawPacket);
        if (optView && optView->protocol() == Backend::ProtocolId::Udp) {
            const auto* meta = optView->getMetadata<Backend::UdpMetadata>();
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
        Common::Compat::span<const MessageTask> tasks,
        std::size_t& invalidEnvelopeCount,
        std::mutex* handlerMutex)
    {
        const std::size_t drainCount = tasks.size();
        std::size_t batchStart = 0;
        while (batchStart < drainCount) {
            std::string_view chName = tasks[batchStart].channelName();
            std::size_t batchEnd = batchStart + 1;
            while (batchEnd < drainCount && tasks[batchEnd].channelName() == chName) {
                ++batchEnd;
            }
            std::size_t spanCount = batchEnd - batchStart;
            std::array<Common::Compat::span<const std::byte>, 16> rawMessages;
            std::array<::Backend::Lease, 16> leases;
            for (std::size_t k = 0; k < spanCount; ++k) {
                rawMessages[k] = Common::Memory::ByteView(tasks[batchStart + k].payload());
                leases[k] = std::move(tasks[batchStart + k].lease());
            }
            Detail::dispatchReceivedBatch(
                handler,
                chName,
                Common::Compat::span<const Common::Compat::span<const std::byte>>(rawMessages.data(), spanCount),
                Common::Compat::span<::Backend::Lease>(leases.data(), spanCount),
                invalidEnvelopeCount,
                handlerMutex);
            batchStart = batchEnd;
        }
    }

    using QueueType = Common::Queue::SpscQueue<MessageTask, 1024>;
    using SharedQueueType = Common::Queue::MpmcQueue<MessageTask, 4096>;

    bool channelFailed(std::string_view channelName, const ::Backend::ReceiveError& error)
    {
        if (::Backend::isReceiveTimeout(error)) {
            return false;
        }
        if (::Backend::isReceiveClosed(error)) {
            Proxy::Log::error("[ThreadPoolStrategy] Input channel '{}' is closed", channelName);
            return true;
        }
        {
            Proxy::Log::error("[ThreadPoolStrategy] Input channel '{}' failed: {}",
                channelName, ::Backend::receiveErrorMessage(error));
            return true;
        }
    }

    bool channelFailed(std::string_view channelName, const ::Backend::BatchResult& result)
    {
        return !result.hasValue() && channelFailed(channelName, result.error().cause);
    }

    /**
     * @brief Ingests datagrams directly into worker queue slot memory (zero-copy) for maximum throughput.
     */
    bool pollUnconstrained(
        std::string_view channelName,
        ::Backend::IInputChannel& channel,
        SharedQueueType* sharedQueue,
        Common::Compat::span<const std::unique_ptr<QueueType>> queues,
        std::atomic<std::size_t>& nextWorker,
        const std::shared_ptr<Common::Memory::BufferPool>& bufferPool,
        Handler::IHandler::Timeout timeout,
        Common::Compat::stop_token stoken)
    {
        if (sharedQueue) {
            constexpr std::size_t maxBatch = 16;
            auto batchBuffers = std::make_unique<std::array<std::array<std::byte, 65535>, maxBatch>>();
            std::array<::Backend::BufferSlot, maxBatch> batchSlots;
            for (std::size_t i = 0; i < maxBatch; ++i) {
                batchSlots[i] = ::Backend::BufferSlot { Common::Memory::MutableByteView((*batchBuffers)[i]), 0 };
            }

            auto result = channel.receiveBatch(
                Common::Compat::span<::Backend::BufferSlot>(batchSlots.data(), maxBatch),
                timeout);
            const bool failed = channelFailed(channelName, result);
            const std::size_t received = result.hasValue() ? result.value() : result.error().receivedCount;
            if (failed && received == 0) {
                return false;
            }

            for (std::size_t i = 0; i < received; ++i) {
                MessageTask task;
                const std::size_t msgSize = batchSlots[i].size;
                if (msgSize > MessageTask::SmallBufferSize && bufferPool) {
                    if (auto slice = bufferPool->acquire()) {
                        std::memcpy(slice->data(), (*batchBuffers)[i].data(), msgSize);
                        task.assignSlice(channelName, std::move(*slice), msgSize);
                    } else {
                        task.assign(channelName, Common::Compat::span<const std::byte>((*batchBuffers)[i].data(), msgSize));
                    }
                } else {
                    task.assign(channelName, Common::Compat::span<const std::byte>((*batchBuffers)[i].data(), msgSize));
                }
                task.setLease(std::move(batchSlots[i].lease));

                if (!sharedQueue->tryPush(std::move(task))) {
                    sharedQueue->push(std::move(task), stoken);
                }
            }
            return !failed;
        }

        constexpr std::size_t maxBatch = 16;
        bool hasOversized = false;
        bool receiveFailed = false;

        auto fillSlots = [&](Common::Compat::span<MessageTask> slots) -> std::size_t {
            std::array<::Backend::BufferSlot, maxBatch> inPlaceSlots;
            const std::size_t n = std::min(slots.size(), maxBatch);
            for (std::size_t i = 0; i < n; ++i) {
                inPlaceSlots[i] = ::Backend::BufferSlot { slots[i].prepareInline(channelName), 0 };
            }
            auto result = channel.receiveBatch(
                Common::Compat::span<::Backend::BufferSlot>(inPlaceSlots.data(), n),
                timeout);
            receiveFailed = channelFailed(channelName, result) || receiveFailed;
            const std::size_t nReceived = result.hasValue() ? result.value() : result.error().receivedCount;
            for (std::size_t i = 0; i < nReceived; ++i) {
                slots[i].commitInline(inPlaceSlots[i].size);
                slots[i].setLease(std::move(inPlaceSlots[i].lease));
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
        if (receiveFailed) {
            return false;
        }
        if (!targetQueue) {
            targetQueue = queues[start % numWorkers].get();
            targetQueue->pushInPlace(maxBatch, fillSlots, stoken);
        }
        if (receiveFailed) {
            return false;
        }

        if (hasOversized && targetQueue) {
            std::array<std::byte, 65535> jumboBuf;
            ::Backend::BufferSlot jumboSlot { Common::Memory::MutableByteView(jumboBuf), 0 };
            constexpr Handler::IHandler::Timeout zeroTimeout { 0 };
            auto jumboResult = channel.receiveSlot(jumboSlot, zeroTimeout);
            if (!jumboResult.hasValue() && channelFailed(channelName, jumboResult.error())) {
                return false;
            }
            std::size_t jumboBytes = jumboResult.hasValue() ? jumboResult.value() : 0;
            if (jumboBytes > 0) {
                targetQueue->pushBatch(
                    1,
                    [&](MessageTask& slot, std::size_t) {
                        slot.setLease(std::move(jumboSlot.lease));
                        if (jumboBytes > MessageTask::SmallBufferSize && bufferPool) {
                            if (auto slice = bufferPool->acquire()) {
                                std::memcpy(slice->data(), jumboBuf.data(), jumboBytes);
                                slot.assignSlice(channelName, std::move(*slice), jumboBytes);
                                return;
                            }
                        }
                        slot.assign(channelName, Common::Compat::span<const std::byte>(jumboBuf.data(), jumboBytes));
                    },
                    stoken);
            }
        }
        return true;
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
        Handler::IHandler::Timeout timeout,
        Common::Compat::stop_token stoken)
    {
        constexpr std::size_t maxBatch = 16;

        // Sized to 16 slots x 65,535 B (~1 MB) to receive max UDP/UDS datagrams for flow inspection without truncation.
        // Sized to maxBatch x 65,535 B to receive full datagram batches without truncation.
        // Allocated on the heap to prevent thread stack overflow, while avoiding recurring allocations in the poll loop.
        auto batchBuffers = std::make_unique<std::array<std::array<std::byte, 65535>, maxBatch>>();
        std::array<::Backend::BufferSlot, maxBatch> batchSlots;
        for (std::size_t i = 0; i < maxBatch; ++i) {
            batchSlots[i] = ::Backend::BufferSlot { Common::Memory::MutableByteView((*batchBuffers)[i]), 0 };
        }

        auto result = channel.receiveBatch(
            Common::Compat::span<::Backend::BufferSlot>(batchSlots.data(), maxBatch),
            timeout);

        const bool failed = channelFailed(channelName, result);
        const std::size_t received = result.hasValue() ? result.value() : result.error().receivedCount;
        if (failed && received == 0) {
            return false;
        }
        if (received == 0) {
            return !failed;
        }

        const std::size_t numWorkers = queues.size();
        std::size_t runStart = 0;
        while (runStart < received) {
            Common::Memory::ByteView firstSpan((*batchBuffers)[runStart].data(), batchSlots[runStart].size);
            const std::size_t workerIdx = computeFlowWorkerIndex(channelIdx, firstSpan, numWorkers);

            std::size_t runEnd = runStart + 1;
            while (runEnd < received) {
                Common::Memory::ByteView nextSpan((*batchBuffers)[runEnd].data(), batchSlots[runEnd].size);
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
                    slot.setLease(std::move(batchSlots[pktIdx].lease));
                    if (pktSize > MessageTask::SmallBufferSize && bufferPool) {
                        if (auto slice = bufferPool->acquire()) {
                            std::memcpy(slice->data(), (*batchBuffers)[pktIdx].data(), pktSize);
                            slot.assignSlice(channelName, std::move(*slice), pktSize);
                            return;
                        }
                    }
                    slot.assign(channelName, Common::Compat::span<const std::byte>((*batchBuffers)[pktIdx].data(), pktSize));
                },
                stoken);

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
    m_bufferPool = std::make_shared<Common::Memory::BufferPool>(65536, 256);
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
        m_workerThreads.emplace_back([this, &handler, i](Common::Compat::stop_token stoken) {
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
                        drainWorkerTasks(
                            handler,
                            Common::Compat::span<const MessageTask>(batchTasks.data(), count),
                            invalidEnvelopeCount,
                            m_serializeHandlerAccess ? &m_handlerMutex : nullptr);
                    } else {
                        std::this_thread::yield();
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
    handler.forEachInputChannel([this, channelTimeout, enforceFifo, &channelIdx](::Backend::IInputChannel& channel) {
        auto* channelPtr = &channel;
        const auto currentChannelIdx = channelIdx++;
        m_channelThreads.emplace_back([this, channelName = channel.name(), channelPtr, currentChannelIdx, channelTimeout, enforceFifo](Common::Compat::stop_token stoken) {
            while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
                if (!enforceFifo) {
                    if (!pollUnconstrained(channelName, *channelPtr, m_sharedQueue.get(), m_workerQueues, m_nextWorker, m_bufferPool, channelTimeout, stoken)) {
                        break;
                    }
                } else {
                    if (!pollStrictFifo(channelName, *channelPtr, currentChannelIdx, m_workerQueues, m_bufferPool, channelTimeout, stoken)) {
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
