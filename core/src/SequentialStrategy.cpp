#include "core/SequentialStrategy.hpp"
#include "core/IHandler.hpp"
#include "log/Log.hpp"
#include <array>
#include <chrono>
#include <span>
#include <thread>

namespace Core {

SequentialStrategy::SequentialStrategy() = default;

SequentialStrategy::~SequentialStrategy()
{
    stop();
    wait();
}

void SequentialStrategy::start(IHandler& handler)
{
    m_running.store(true, std::memory_order_release);
    constexpr IHandler::Timeout channelTimeout { 50 };

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
                        std::array<std::array<std::byte, 2048>, 16> batchBuffers;
                        std::array<std::span<std::byte>, 16> destSpans;
                        for (std::size_t i = 0; i < 16; ++i) {
                            destSpans[i] = std::span<std::byte>(batchBuffers[i].data(), batchBuffers[i].size());
                        }
                        std::array<std::size_t, 16> batchSizes;

                        std::size_t batchCount = namedChannel.channel->receiveBatch(
                            std::span<const std::span<std::byte>>(destSpans.data(), destSpans.size()),
                            batchSizes,
                            channelTimeout);

                        if (batchCount > 0) {
                            std::array<std::span<const std::byte>, 16> batchSpans;
                            bool hasOversized = false;
                            for (std::size_t i = 0; i < batchCount; ++i) {
                                batchSpans[i] = std::span<const std::byte>(batchBuffers[i].data(), batchSizes[i]);
                                if (batchSizes[i] == batchBuffers[i].size()) {
                                    hasOversized = true;
                                }
                            }
                            handler.onBatchReceived(namedChannel.name, std::span<const std::span<const std::byte>>(batchSpans.data(), batchCount));

                            if (hasOversized) {
                                static thread_local std::array<std::byte, 65535> jumboBuffer;
                                constexpr IHandler::Timeout zeroTimeout { 0 };
                                std::size_t jumboBytes = namedChannel.channel->receiveInto(jumboBuffer, zeroTimeout);
                                if (jumboBytes > 0) {
                                    handler.onMessageReceived(namedChannel.name, std::span<const std::byte>(jumboBuffer.data(), jumboBytes));
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

} // namespace Core
