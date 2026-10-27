#include "core/threading/SequentialStrategy.hpp"
#include "MessageDispatcher.hpp"
#include "core/handler/IHandler.hpp"
#include "log/Log.hpp"
#include <array>
#include <chrono>
#include <span>
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

    m_workerThread = std::jthread([this, &handler, channelCount, channelTimeout](std::stop_token stoken) {
        // Sized to 16 slots x 65,535 B (~1 MB) to receive max UDP/UDS datagrams without truncation.
        // Allocated once on the heap when the thread starts to prevent thread stack overflow,
        // while keeping the buffer resident across polling loops with zero per-message allocations.
        auto batchBuffers = std::make_unique<std::array<std::array<std::byte, 65535>, 16>>();
        std::array<::Backend::BufferSlot, 16> batchSlots;
        for (std::size_t i = 0; i < 16; ++i) {
            batchSlots[i] = ::Backend::BufferSlot { std::span<std::byte>((*batchBuffers)[i].data(), (*batchBuffers)[i].size()), 0 };
        }
        std::array<std::span<const std::byte>, 16> rawMessages;
        std::size_t invalidEnvelopeCount = 0;

        while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
            handler.forEachInputChannel([&](::Backend::IInputChannel& channel) {
                if (stoken.stop_requested() || !m_running.load(std::memory_order_acquire)) {
                    return;
                }

                std::size_t batchCount = channel.receiveBatch(
                    std::span<::Backend::BufferSlot>(batchSlots.data(), batchSlots.size()),
                    channelTimeout);

                if (batchCount == 0) {
                    return;
                }

                for (std::size_t i = 0; i < batchCount; ++i) {
                    rawMessages[i] = std::span<const std::byte>((*batchBuffers)[i].data(), batchSlots[i].size);
                }
                Detail::dispatchReceivedBatch(
                    handler,
                    channel.name(),
                    std::span<const std::span<const std::byte>>(rawMessages.data(), batchCount),
                    invalidEnvelopeCount,
                    nullptr);
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
