#include "core/threading/SequentialStrategy.hpp"
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
    Proxy::Log::info("[SequentialStrategy] Started loop for {} channel(s)", channelCount);

    m_workerThread = Common::Compat::jthread([this, &handler, channelCount, channelTimeout](Common::Compat::stop_token stoken) {
        // Sized to 16 slots x 65,535 B (~1 MB) to receive max UDP/UDS datagrams without truncation.
        // Allocated once on the heap when the thread starts to prevent thread stack overflow,
        // while keeping the buffer resident across polling loops with zero per-message allocations.
        auto batchBuffers = std::make_unique<std::array<std::array<std::byte, 65535>, 16>>();
        std::array<::Backend::BufferSlot, 16> batchSlots;
        for (std::size_t i = 0; i < 16; ++i) {
            batchSlots[i] = ::Backend::BufferSlot { Common::Memory::MutableByteView((*batchBuffers)[i]), 0 };
        }
        std::array<Common::Compat::span<const std::byte>, 16> rawMessages;
        std::size_t invalidEnvelopeCount = 0;

        while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
            handler.forEachInputChannel([&](::Backend::IInputChannel& channel) {
                if (stoken.stop_requested() || !m_running.load(std::memory_order_acquire)) {
                    return;
                }

                auto result = channel.receiveBatch(
                    Common::Compat::span<::Backend::BufferSlot>(batchSlots.data(), batchSlots.size()),
                    channelTimeout);

                const std::size_t batchCount = result.hasValue() ? result.value() : result.error().receivedCount;
                if (batchCount > 0) {
                    for (std::size_t i = 0; i < batchCount; ++i) {
                        rawMessages[i] = Common::Memory::ByteView((*batchBuffers)[i].data(), batchSlots[i].size);
                    }
                    Detail::dispatchReceivedBatch(
                        handler,
                        channel.name(),
                        Common::Compat::span<const Common::Compat::span<const std::byte>>(rawMessages.data(), batchCount),
                        invalidEnvelopeCount,
                        nullptr);
                }

                if (result.hasValue() || ::Backend::isReceiveTimeout(result.error().cause)) {
                    return;
                }
                if (::Backend::isReceiveClosed(result.error().cause)) {
                    Proxy::Log::error("[SequentialStrategy] Input channel '{}' is closed", channel.name());
                } else {
                    Proxy::Log::error("[SequentialStrategy] Input channel '{}' failed: {}",
                        channel.name(), ::Backend::receiveErrorMessage(result.error().cause));
                }
                m_running.store(false, std::memory_order_release);
            });

            if (channelCount == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            handler.step();
        }
        Proxy::Log::info("[SequentialStrategy] Loop stopped.");
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
