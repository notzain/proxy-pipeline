#include "core/threading/ThreadPoolStrategy.hpp"
#include "core/handler/IHandler.hpp"
#include "core/message/MessageView.hpp"
#include "core/message/UdpMetadata.hpp"
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
                    ++invalidEnvelopeCount;
                    if (invalidEnvelopeCount <= 8 || (invalidEnvelopeCount & 1023) == 0) {
                        Proxy::Log::warn("[ThreadPoolStrategy] Channel '{}' task received invalid envelope ({} bytes, total dropped: {}).",
                            chName, payloadSpan.size(), invalidEnvelopeCount);
                    }
                }
            }
            if (validViews > 0) {
                handler.onBatchReceived(chName, std::span<const Core::Message::MessageView>(views.data(), validViews));
            }
            batchStart = batchEnd;
        }
    }

    using QueueType = Core::Queue::SpscQueue<MessageTask, 1024>;

    /**
     * @brief Ingests datagrams directly into worker queue slot memory (zero-copy) for maximum throughput.
     */
    void pollUnconstrained(
        const Handler::IHandler::NamedInputChannel& namedChannel,
        std::span<const std::unique_ptr<QueueType>> queues,
        std::atomic<std::size_t>& nextWorker,
        Handler::IHandler::Timeout timeout,
        std::stop_token stoken)
    {
        constexpr std::size_t maxBatch = 16;
        bool hasOversized = false;

        auto fillSlots = [&](std::span<MessageTask> slots) -> std::size_t {
            std::array<std::span<std::byte>, maxBatch> inPlaceSpans;
            std::array<std::size_t, maxBatch> inPlaceSizes { };
            const std::size_t n = std::min(slots.size(), maxBatch);
            for (std::size_t i = 0; i < n; ++i) {
                inPlaceSpans[i] = slots[i].prepareInline(namedChannel.name);
            }
            const std::size_t nReceived = namedChannel.channel->receiveBatch(
                std::span<const std::span<std::byte>>(inPlaceSpans.data(), n),
                std::span<std::size_t>(inPlaceSizes.data(), n),
                timeout);
            for (std::size_t i = 0; i < nReceived; ++i) {
                slots[i].commitInline(inPlaceSizes[i]);
                if (inPlaceSizes[i] == MessageTask::SmallBufferSize) {
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
            // Sized to 65,535 B to capture the full MTU payload when a packet overflows MessageTask's 2048 B inline buffer.
            // alignas(64): Prevents unaligned boundary traversal when copying jumbo datagrams into the heap buffer.
            // static thread_local: Avoids allocating 65 KB on this poller's stack on every oversized message burst.
            alignas(64) static thread_local std::array<std::byte, 65535> jumboBuf;
            constexpr Handler::IHandler::Timeout zeroTimeout { 0 };
            std::size_t jumboBytes = namedChannel.channel->receiveInto(jumboBuf, zeroTimeout);
            if (jumboBytes > 0) {
                targetQueue->pushBatch(
                    1,
                    [&](MessageTask& slot, std::size_t) {
                        slot.assign(namedChannel.name, std::span<const std::byte>(jumboBuf.data(), jumboBytes));
                    },
                    stoken);
            }
        }
    }

    /**
     * @brief Ingests datagrams and groups them by client flow / channel to guarantee strict FIFO ordering.
     */
    void pollStrictFifo(
        const Handler::IHandler::NamedInputChannel& namedChannel,
        std::size_t channelIdx,
        std::span<const std::unique_ptr<QueueType>> queues,
        Handler::IHandler::Timeout timeout,
        std::stop_token stoken)
    {
        constexpr std::size_t maxBatch = 16;

        // Sized to 16 slots x 65,535 B (~1 MB) to receive max UDP/UDS datagrams for flow inspection without truncation.
        // alignas(64): Ensures 64-byte boundary alignment so ingress socket operations and worker queues do not straddle cache lines.
        // static thread_local: Avoids placing ~1 MB on the poller thread's stack (which could trigger a stack overflow).
        alignas(64) static thread_local std::array<std::array<std::byte, 65535>, maxBatch> batchBuffers;
        std::array<std::span<std::byte>, maxBatch> destSpans;
        for (std::size_t i = 0; i < maxBatch; ++i) {
            destSpans[i] = std::span<std::byte>(batchBuffers[i].data(), batchBuffers[i].size());
        }
        // alignas(64): Aligns the size array to a cache line so batch size updates don't false-share with loop control state.
        alignas(64) std::array<std::size_t, maxBatch> sizes { };

        const std::size_t received = namedChannel.channel->receiveBatch(
            std::span<const std::span<std::byte>>(destSpans.data(), maxBatch),
            std::span<std::size_t>(sizes.data(), maxBatch),
            timeout);

        if (received == 0) {
            return;
        }

        const std::size_t numWorkers = queues.size();
        std::size_t runStart = 0;
        while (runStart < received) {
            std::span<const std::byte> firstSpan(batchBuffers[runStart].data(), sizes[runStart]);
            const std::size_t workerIdx = computeFlowWorkerIndex(channelIdx, firstSpan, numWorkers);

            std::size_t runEnd = runStart + 1;
            while (runEnd < received) {
                std::span<const std::byte> nextSpan(batchBuffers[runEnd].data(), sizes[runEnd]);
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
                    slot.assign(namedChannel.name, std::span<const std::byte>(batchBuffers[pktIdx].data(), sizes[pktIdx]));
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
}

void ThreadPoolStrategy::setStrictFifo(bool enable) noexcept
{
    m_strictFifo = enable;
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

    const auto& channels = handler.getInputChannels();
    Proxy::Log::info("[ThreadPoolStrategy] Starting with {} pool workers and {} channel poller(s)...",
        m_numWorkerThreads, channels.size());

    // 1. Launch pool worker threads with partitioned queues
    for (std::size_t i = 0; i < m_numWorkerThreads; ++i) {
        m_workerThreads.emplace_back([this, &handler, i](std::stop_token stoken) {
            auto& myQueue = *m_workerQueues[i];
            std::size_t invalidEnvelopeCount = 0;

            while (!stoken.stop_requested()) {
                myQueue.drainContiguous(16, [&handler, &invalidEnvelopeCount](std::span<const MessageTask> tasks) { drainWorkerTasks(handler, tasks, invalidEnvelopeCount); }, stoken);
            }
        });
    }

    // 2. Launch channel poller threads to enqueue message tasks
    const bool enforceFifo = m_strictFifo;
    for (std::size_t channelIdx = 0; channelIdx < channels.size(); ++channelIdx) {
        const auto& namedChannel = channels[channelIdx];
        m_channelThreads.emplace_back([this, &namedChannel, channelIdx, channelTimeout, enforceFifo](std::stop_token stoken) {
            while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
                if (!namedChannel.channel) {
                    continue;
                }

                if (!enforceFifo) {
                    pollUnconstrained(namedChannel, m_workerQueues, m_nextWorker, channelTimeout, stoken);
                } else {
                    pollStrictFifo(namedChannel, channelIdx, m_workerQueues, channelTimeout, stoken);
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
