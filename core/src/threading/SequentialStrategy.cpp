#include "core/threading/SequentialStrategy.hpp"
#include "core/handler/IHandler.hpp"
#include "core/message/MessageView.hpp"
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

    const auto& channels = handler.getInputChannels();
    Proxy::Log::info("[SequentialStrategy] Started loop for {} channel(s)", channels.size());

    m_workerThread = std::jthread([this, &handler, &channels, channelTimeout](std::stop_token stoken) {
        while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
            if (!channels.empty()) {
                for (const auto& namedChannel : channels) {
                    if (stoken.stop_requested() || !m_running.load(std::memory_order_acquire)) {
                        break;
                    }
                    if (namedChannel.channel) {
                        alignas(64) std::array<std::array<std::byte, 2048>, 16> batchBuffers;
                        std::array<std::span<std::byte>, 16> destSpans;
                        for (std::size_t i = 0; i < 16; ++i) {
                            destSpans[i] = std::span<std::byte>(batchBuffers[i].data(), batchBuffers[i].size());
                        }
                        alignas(64) std::array<std::size_t, 16> batchSizes;

                        std::size_t batchCount = namedChannel.channel->receiveBatch(
                            std::span<const std::span<std::byte>>(destSpans.data(), destSpans.size()),
                            batchSizes,
                            channelTimeout);

                        if (batchCount > 0) {
                            std::array<Core::Message::MessageView, 16> views;
                            std::size_t validViews = 0;
                            bool hasOversized = false;
                            for (std::size_t i = 0; i < batchCount; ++i) {
                                std::span<const std::byte> span(batchBuffers[i].data(), batchSizes[i]);
                                if (batchSizes[i] == batchBuffers[i].size()) {
                                    hasOversized = true;
                                }
                                auto optView = Core::Message::MessageView::from(span);
                                if (optView) {
                                    views[validViews++] = *optView;
                                } else {
                                    Proxy::Log::warn("[SequentialStrategy] Channel '{}' received invalid envelope ({} bytes). Dropping.", namedChannel.name, batchSizes[i]);
                                }
                            }

                            if (validViews > 0) {
                                handler.onBatchReceived(namedChannel.name, std::span<const Core::Message::MessageView>(views.data(), validViews));
                            }

                            if (hasOversized) {
                                static thread_local std::array<std::byte, 65535> jumboBuffer;
                                constexpr Handler::IHandler::Timeout zeroTimeout { 0 };
                                std::size_t jumboBytes = namedChannel.channel->receiveInto(jumboBuffer, zeroTimeout);
                                if (jumboBytes > 0) {
                                    std::span<const std::byte> jumboSpan(jumboBuffer.data(), jumboBytes);
                                    auto optView = Core::Message::MessageView::from(jumboSpan);
                                    if (optView) {
                                        handler.onMessageReceived(namedChannel.name, *optView);
                                    } else {
                                        Proxy::Log::warn("[SequentialStrategy] Channel '{}' received invalid envelope ({} bytes). Dropping.", namedChannel.name, jumboBytes);
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
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
