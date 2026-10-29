#include "core/threading/SimpleExecutionStrategy.hpp"
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
    Proxy::Log::info("[SimpleExecutionStrategy] Starting deterministic polling loop for {} channel(s)", channelCount);

    m_workerThread = Common::Compat::jthread([this, &handler, channelCount, channelTimeout](Common::Compat::stop_token stoken) {
        // Pre-allocated stack buffer for single datagram reception to maintain 100% predictable trace/debug stacks
        std::array<std::byte, 65535> receiveBuffer;
        ::Backend::BufferSlot slot { Common::Memory::MutableByteView(receiveBuffer), 0 };
        std::size_t invalidEnvelopeCount = 0;

        while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
            handler.forEachInputChannel([&](::Backend::IInputChannel& channel) {
                if (stoken.stop_requested() || !m_running.load(std::memory_order_acquire)) {
                    return;
                }

                auto result = channel.receiveSlot(slot, channelTimeout);

                if (result.hasValue()) {
                    const std::size_t receivedBytes = result.value();
                    if (receivedBytes > 0) {
                        auto msgSpan = Common::Memory::ByteView(receiveBuffer.data(), receivedBytes);
                        std::array<Common::Compat::span<const std::byte>, 1> batch { msgSpan };
                        Detail::dispatchReceivedBatch(
                            handler,
                            channel.name(),
                            Common::Compat::span<const Common::Compat::span<const std::byte>>(batch.data(), batch.size()),
                            Common::Compat::span<::Backend::Lease>(&slot.lease, 1),
                            invalidEnvelopeCount,
                            nullptr);
                    }
                } else {
                    const auto cause = result.error();
                    if (!::Backend::isReceiveTimeout(cause)) {
                        if (::Backend::isReceiveClosed(cause)) {
                            Proxy::Log::error("[SimpleExecutionStrategy] Input channel '{}' is closed", channel.name());
                        } else {
                            Proxy::Log::error("[SimpleExecutionStrategy] Input channel '{}' failed: {}",
                                channel.name(), ::Backend::receiveErrorMessage(cause));
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
        Proxy::Log::info("[SimpleExecutionStrategy] Deterministic loop stopped.");
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
