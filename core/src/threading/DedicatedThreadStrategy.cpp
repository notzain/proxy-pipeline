#include "core/threading/DedicatedThreadStrategy.hpp"
#include "core/handler/IHandler.hpp"
#include "core/message/MessageView.hpp"
#include "log/Log.hpp"
#include <array>
#include <chrono>
#include <span>

namespace Core::Threading {

namespace {

    /**
     * @brief Parses raw buffers into validated MessageViews and dispatches to handler.
     *
     * Drops malformed frames while rate-limiting error logs to prevent log saturation.
     */
    void dispatchValidatedBatch(
        Handler::IHandler& handler,
        std::string_view channelName,
        std::span<const std::array<std::byte, 65535>> buffers,
        std::span<const std::size_t> sizes,
        std::size_t count,
        std::size_t& invalidEnvelopeCount)
    {
        std::array<Core::Message::MessageView, 16> views;
        std::size_t validViews = 0;
        for (std::size_t i = 0; i < count; ++i) {
            std::span<const std::byte> span(buffers[i].data(), sizes[i]);
            auto optView = Core::Message::MessageView::from(span);
            if (optView) {
                views[validViews++] = *optView;
            } else {
                ++invalidEnvelopeCount;
                if (invalidEnvelopeCount <= 8 || (invalidEnvelopeCount & 1023) == 0) {
                    Proxy::Log::warn("[DedicatedThreadStrategy] Channel '{}' received invalid envelope ({} bytes, total dropped: {}).",
                        channelName, sizes[i], invalidEnvelopeCount);
                }
            }
        }

        if (validViews > 0) {
            handler.onBatchReceived(channelName, std::span<const Core::Message::MessageView>(views.data(), validViews));
        }
    }

} // namespace

DedicatedThreadStrategy::DedicatedThreadStrategy() = default;

DedicatedThreadStrategy::~DedicatedThreadStrategy()
{
    stop();
    wait();
}

void DedicatedThreadStrategy::start(Handler::IHandler& handler)
{
    m_running.store(true, std::memory_order_release);
    constexpr Handler::IHandler::Timeout channelTimeout { 50 };

    const auto& channels = handler.getInputChannels();
    Proxy::Log::info("[DedicatedThreadStrategy] Launching dedicated threads for {} channel(s)...", channels.size());

    for (const auto& namedChannel : channels) {
        m_workerThreads.emplace_back([this, &handler, &namedChannel, channelTimeout](std::stop_token stoken) {
            Proxy::Log::debug("[DedicatedThreadStrategy] Poller thread started for channel '{}'", namedChannel.name);

            // Sized to 16 slots x 65,535 B (~1 MB) to receive max UDP/UDS datagrams without truncation.
            // alignas(64): Prevents individual slot buffers from crossing hardware cache line boundaries during burst DMA/copy.
            // static thread_local: Avoids placing a 1 MB buffer on this channel thread's stack (risking overflow) and avoids recurring heap allocations.
            alignas(64) static thread_local std::array<std::array<std::byte, 65535>, 16> batchBuffers;
            std::array<std::span<std::byte>, 16> destSpans;
            for (std::size_t i = 0; i < 16; ++i) {
                destSpans[i] = std::span<std::byte>(batchBuffers[i].data(), batchBuffers[i].size());
            }
            // alignas(64): Aligns the size array to a cache line so atomic/SIMD fills don't false-share with loop control state.
            alignas(64) std::array<std::size_t, 16> batchSizes {};
            std::size_t invalidEnvelopeCount = 0;

            while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
                if (!namedChannel.channel) {
                    continue;
                }
                std::size_t batchCount = namedChannel.channel->receiveBatch(
                    std::span<const std::span<std::byte>>(destSpans.data(), destSpans.size()),
                    batchSizes,
                    channelTimeout);

                if (batchCount > 0) {
                    dispatchValidatedBatch(
                        handler,
                        namedChannel.name,
                        std::span<const std::array<std::byte, 65535>>(batchBuffers.data(), batchCount),
                        std::span<const std::size_t>(batchSizes.data(), batchCount),
                        batchCount,
                        invalidEnvelopeCount);
                }
            }

            Proxy::Log::debug("[DedicatedThreadStrategy] Poller thread exited for channel '{}'", namedChannel.name);
        });
    }

    m_workerThreads.emplace_back([this, &handler](std::stop_token stoken) {
        while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
            handler.step();
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
