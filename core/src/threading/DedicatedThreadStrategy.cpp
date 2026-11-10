#include "core/threading/DedicatedThreadStrategy.hpp"
#include "IngressBatchScratch.hpp"
#include "MessageDispatcher.hpp"
#include "common/compat/Span.hpp"
#include "common/memory/ByteView.hpp"
#include "common/memory/MutableByteView.hpp"
#include "common/memory/ParallelArray.hpp"
#include "core/handler/IHandler.hpp"
#include "log/Log.hpp"
#include <array>
#include <chrono>
#include <utility>

namespace Core::Threading {

DedicatedThreadStrategy::DedicatedThreadStrategy(bool serializeHandlerAccess)
    : m_serializeHandlerAccess(serializeHandlerAccess)
{
}

DedicatedThreadStrategy::~DedicatedThreadStrategy()
{
    stop();
    wait();
}

void DedicatedThreadStrategy::start(Handler::IHandler& handler)
{
    m_running.store(true, std::memory_order_release);
    constexpr Handler::IHandler::Timeout channelTimeout { 50 };

    const auto channelCount = handler.inputChannelCount();
    PROXY_LOG_INFO("Starting dedicated thread strategy (channels: {channel_count})", channelCount);

    handler.forEachInputChannel([this, &handler, channelTimeout](::Backend::IInputChannel& channel) {
        auto* channelPtr = &channel;
        m_workerThreads.emplace_back([this, &handler, channelName = channel.name(), channelPtr, channelTimeout](Common::Compat::stop_token stoken) {
            PROXY_LOG_DEBUG("Poller thread started for channel '{channel}'", channelName);

            // Thread-private 16-slot scratch buffer; never allocated if this channel provides its own memory.
            Detail::IngressBatchScratch<16> scratch;
            Common::Memory::ParallelArray<16, Common::Compat::span<const std::byte>, ::Backend::PayloadFormat, ::Backend::Lease> batch;
            std::size_t invalidEnvelopeCount = 0;

            while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
                // Synchronous dispatch: handler consumes the batch on this thread before the next poll.
                auto slots = scratch.prepareSync(*channelPtr);
                auto result = channelPtr->receiveBatch(slots, channelTimeout);

                const std::size_t batchCount = result.isOk() ? result.unwrap() : result.unwrapErr().receivedCount;
                if (batchCount > 0) {
                    batch.clear();
                    for (std::size_t i = 0; i < batchCount; ++i) {
                        batch.push_back(slots[i].filledSpan(), slots[i].format(), std::move(slots[i].lease));
                    }
                    Detail::dispatchReceivedBatch(
                        handler,
                        channelName,
                        batch.span<0>(),
                        batch.span<1>(),
                        batch.span<2>(),
                        invalidEnvelopeCount,
                        m_serializeHandlerAccess ? &m_handlerMutex : nullptr);
                }
                if (result.isOk() || result.unwrapErr().cause.is<Common::Net::ReceiveTimeout>()) {
                    continue;
                }
                if (result.unwrapErr().cause.is<Common::Net::ReceiveClosed>()) {
                    PROXY_LOG_ERROR("Input channel '{channel}' is closed", channelName);
                    break;
                }
                PROXY_LOG_ERROR("Input channel '{channel}' failed: {error_message}",
                    channelName, result.unwrapErr().cause.message());
                break;
            }

            PROXY_LOG_DEBUG("Poller thread exited for channel '{channel}'", channelName);
        });
    });

    m_workerThreads.emplace_back([this, &handler](Common::Compat::stop_token stoken) {
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

    PROXY_LOG_INFO("Dedicated worker threads active (workers: {worker_count})", m_workerThreads.size());
}

void DedicatedThreadStrategy::stop()
{
    m_running.store(false, std::memory_order_release);
    for (auto& t : m_workerThreads) {
        t.request_stop();
    }
}

void DedicatedThreadStrategy::wait()
{
    for (auto& t : m_workerThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
    m_workerThreads.clear();
}

bool DedicatedThreadStrategy::isRunning() const noexcept
{
    return m_running.load(std::memory_order_acquire);
}

} // namespace Core::Threading
