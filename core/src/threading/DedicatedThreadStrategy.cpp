#include "core/threading/DedicatedThreadStrategy.hpp"
#include "MessageDispatcher.hpp"
#include "core/handler/IHandler.hpp"
#include "log/Log.hpp"
#include <array>
#include <chrono>
#include <span>

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
    Proxy::Log::info("[DedicatedThreadStrategy] Launching dedicated threads for {} channel(s)...", channelCount);

    handler.forEachInputChannel([this, &handler, channelTimeout](::Backend::IInputChannel& channel) {
        auto* channelPtr = &channel;
        m_workerThreads.emplace_back([this, &handler, channelName = channel.name(), channelPtr, channelTimeout](std::stop_token stoken) {
            Proxy::Log::debug("[DedicatedThreadStrategy] Poller thread started for channel '{}'", channelName);

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
                std::size_t batchCount = channelPtr->receiveBatch(
                    std::span<::Backend::BufferSlot>(batchSlots.data(), batchSlots.size()),
                    channelTimeout);

                if (batchCount > 0) {
                    for (std::size_t i = 0; i < batchCount; ++i) {
                        rawMessages[i] = std::span<const std::byte>((*batchBuffers)[i].data(), batchSlots[i].size);
                    }
                    Detail::dispatchReceivedBatch(
                        handler,
                        channelName,
                        std::span<const std::span<const std::byte>>(rawMessages.data(), batchCount),
                        invalidEnvelopeCount,
                        m_serializeHandlerAccess ? &m_handlerMutex : nullptr);
                }
            }

            Proxy::Log::debug("[DedicatedThreadStrategy] Poller thread exited for channel '{}'", channelName);
        });
    });

    m_workerThreads.emplace_back([this, &handler](std::stop_token stoken) {
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

    Proxy::Log::info("[DedicatedThreadStrategy] All {} worker thread(s) active.", m_workerThreads.size());
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
