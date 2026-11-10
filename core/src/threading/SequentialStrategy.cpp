#include "core/threading/SequentialStrategy.hpp"
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
#include <thread>
#include <utility>

namespace Core::Threading {

SequentialStrategy::SequentialStrategy() = default;

SequentialStrategy::~SequentialStrategy()
{
    stop();
    wait();
}

void SequentialStrategy::start(Handler::IHandler& handler)
{
    m_running.store(true, std::memory_order_release);
    constexpr Handler::IHandler::Timeout channelTimeout { 50 };

    const auto channelCount = handler.inputChannelCount();
    PROXY_LOG_INFO("Started loop for {} channel(s)", channelCount);

    m_workerThread = Common::Compat::jthread([this, &handler, channelCount, channelTimeout](Common::Compat::stop_token stoken) {
        // Reuses one L2/L3-hot 16x64KB scratch buffer across all channels because each batch is
        // dispatched synchronously before the next receiveBatch() overwrites it.
        Detail::IngressBatchScratch<16> scratch;
        Common::Memory::ParallelArray<16, Common::Compat::span<const std::byte>, ::Backend::PayloadFormat, ::Backend::Lease> batch;
        std::size_t invalidEnvelopeCount = 0;

        while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
            handler.forEachInputChannel([&](::Backend::IInputChannel& channel) {
                if (stoken.stop_requested() || !m_running.load(std::memory_order_acquire)) {
                    return;
                }

                auto slots = scratch.prepareSync(channel);
                auto result = channel.receiveBatch(slots, channelTimeout);

                // Even on partial error, process any messages already drained before handling the error.
                const std::size_t batchCount = result.isOk() ? result.unwrap() : result.unwrapErr().receivedCount;
                if (batchCount > 0) {
                    batch.clear();
                    for (std::size_t i = 0; i < batchCount; ++i) {
                        batch.push_back(slots[i].filledSpan(), slots[i].format(), std::move(slots[i].lease));
                    }
                    Detail::dispatchReceivedBatch(
                        handler,
                        channel.name(),
                        batch.span<0>(),
                        batch.span<1>(),
                        batch.span<2>(),
                        invalidEnvelopeCount,
                        nullptr);
                }

                if (result.isOk() || result.unwrapErr().cause.is<Common::Net::ReceiveTimeout>()) {
                    return;
                }
                if (result.unwrapErr().cause.is<Common::Net::ReceiveClosed>()) {
                    PROXY_LOG_ERROR("Input channel '{}' is closed", channel.name());
                } else {
                    PROXY_LOG_ERROR("Input channel '{}' failed: {}",
                        channel.name(), result.unwrapErr().cause.message());
                }
                m_running.store(false, std::memory_order_release);
            });

            if (channelCount == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            handler.step();
        }
        PROXY_LOG_INFO("Loop stopped.");
    });
}

void SequentialStrategy::stop()
{
    m_running.store(false, std::memory_order_release);
    m_workerThread.request_stop();
}

void SequentialStrategy::wait()
{
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

bool SequentialStrategy::isRunning() const noexcept
{
    return m_running.load(std::memory_order_acquire);
}

} // namespace Core::Threading
