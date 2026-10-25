#include "core/threading/DedicatedThreadStrategy.hpp"
#include "core/handler/IHandler.hpp"
#include "core/message/MessageView.hpp"
#include "core/telemetry/MetricsRegistry.hpp"
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
        static auto& s_msgCounter = Telemetry::MetricsRegistry::instance().counter("messages_ingress_total", "Total messages received at ingress");
        static auto& s_bytesCounter = Telemetry::MetricsRegistry::instance().counter("bytes_ingress_total", "Total bytes received at ingress");
        static auto& s_droppedCounter = Telemetry::MetricsRegistry::instance().counter("messages_dropped_total", "Total malformed messages dropped");

        std::array<Core::Message::MessageView, 16> views;
        std::size_t validViews = 0;
        std::size_t batchBytes = 0;

        for (std::size_t i = 0; i < count; ++i) {
            std::span<const std::byte> span(buffers[i].data(), sizes[i]);
            auto optView = Core::Message::MessageView::from(span);
            if (optView) {
                views[validViews++] = *optView;
                batchBytes += sizes[i];
            } else {
                ++invalidEnvelopeCount;
                s_droppedCounter.increment();
                if (invalidEnvelopeCount <= 8 || (invalidEnvelopeCount & 1023) == 0) {
                    Proxy::Log::warn("[DedicatedThreadStrategy] Channel '{}' received invalid envelope ({} bytes, total dropped: {}).",
                        channelName, sizes[i], invalidEnvelopeCount);
                }
            }
        }

        if (validViews > 0) {
            static auto& s_latencyHist = Telemetry::MetricsRegistry::instance().latencyHistogram(
                "message_processing_duration_us", "Message transit and processing duration in microseconds");

            uint64_t nowNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                    .count());

            for (std::size_t i = 0; i < validViews; ++i) {
                const auto& msg = views[i];
                if (msg.timestampNs() > 0 && nowNs >= msg.timestampNs()) {
                    double durationUs = static_cast<double>(nowNs - msg.timestampNs()) / 1000.0;
                    s_latencyHist.observe(durationUs);
                }
            }

            s_msgCounter.increment(validViews);
            s_bytesCounter.increment(batchBytes);
            handler.processBatch(channelName, std::span<const Core::Message::MessageView>(views.data(), validViews));
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
            // Allocated once on the heap when the thread starts to prevent thread stack overflow,
            // while keeping the buffer resident across polling loops with zero per-message allocations.
            auto batchBuffers = std::make_unique<std::array<std::array<std::byte, 65535>, 16>>();
            std::array<std::span<std::byte>, 16> destSpans;
            for (std::size_t i = 0; i < 16; ++i) {
                destSpans[i] = std::span<std::byte>((*batchBuffers)[i].data(), (*batchBuffers)[i].size());
            }
            // alignas(64): Aligns the size array to a cache line so atomic/SIMD fills don't false-share with loop control state.
            alignas(64) std::array<std::size_t, 16> batchSizes { };
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
                        std::span<const std::array<std::byte, 65535>>(batchBuffers->data(), batchCount),
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
