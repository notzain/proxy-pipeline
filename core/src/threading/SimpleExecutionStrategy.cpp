#include "core/threading/SimpleExecutionStrategy.hpp"
#include "IngressBatchScratch.hpp"
#include "MessageDispatcher.hpp"
#include "common/compat/Span.hpp"
#include "common/memory/ByteView.hpp"
#include "common/memory/MutableByteView.hpp"
#include "core/handler/IHandler.hpp"
#include "log/Log.hpp"
#include <array>
#include <chrono>
#include <thread>

namespace Core::Threading {

SimpleExecutionStrategy::~SimpleExecutionStrategy()
{
    stop();
    wait();
}

void SimpleExecutionStrategy::start(Handler::IHandler& handler)
{
    m_running.store(true, std::memory_order_release);
    constexpr Handler::IHandler::Timeout channelTimeout { 50 };

    const auto channelCount = handler.inputChannelCount();
    PROXY_LOG_INFO("Starting deterministic polling loop (channels: {channel_count})", channelCount);

    m_workerThread = Common::Compat::jthread([this, &handler, channelCount, channelTimeout](Common::Compat::stop_token stoken) {
        // Single-slot scratch buffer reused across polls; skipped if the channel owns its memory (e.g. Vendor).
        Detail::IngressBatchScratch<1> scratch;
        std::size_t invalidEnvelopeCount = 0;

        while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
            handler.forEachInputChannel([&](::Backend::IInputChannel& channel) {
                if (stoken.stop_requested() || !m_running.load(std::memory_order_acquire)) {
                    return;
                }

                // Reset slot state and bind caller buffer only if the channel writes into caller memory.
                auto& slot = scratch.prepareSync(channel)[0];
                auto result = channel.receiveSlot(slot, channelTimeout);

                if (result.isOk()) {
                    const std::size_t receivedBytes = result.unwrap();
                    if (receivedBytes > 0 || slot.isLeased()) {
                        auto msgSpan = slot.filledSpan();
                        std::array<Common::Compat::span<const std::byte>, 1> batch { msgSpan };
                        const std::array<::Backend::PayloadFormat, 1> formats { slot.format() };
                        Detail::dispatchReceivedBatch(
                            handler,
                            channel.name(),
                            Common::Compat::span<const Common::Compat::span<const std::byte>>(batch.data(), batch.size()),
                            Common::Compat::span<const ::Backend::PayloadFormat>(formats.data(), formats.size()),
                            Common::Compat::span<::Backend::Lease>(&slot.lease, 1),
                            invalidEnvelopeCount,
                            nullptr);
                    }
                } else {
                    const auto& cause = result.unwrapErr();
                    if (!cause.is<Common::Net::ReceiveTimeout>()) {
                        if (cause.is<Common::Net::ReceiveClosed>()) {
                            PROXY_LOG_ERROR("Input channel '{channel}' is closed", channel.name());
                        } else {
                            PROXY_LOG_ERROR("Input channel '{channel}' failed: {error_message}",
                                channel.name(), cause.message());
                        }
                        m_running.store(false, std::memory_order_release);
                    }
                }
            });

            if (channelCount == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            handler.step();
        }
        PROXY_LOG_INFO("Deterministic loop stopped.");
    });
}

void SimpleExecutionStrategy::stop()
{
    m_running.store(false, std::memory_order_release);
    m_workerThread.request_stop();
}

void SimpleExecutionStrategy::wait()
{
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
}

bool SimpleExecutionStrategy::isRunning() const noexcept
{
    return m_running.load(std::memory_order_acquire);
}

} // namespace Core::Threading
