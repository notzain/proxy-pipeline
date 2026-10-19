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
                        thread_local std::array<std::byte, 65535> rxBuffer;
                        std::array<std::array<std::byte, 2048>, 16> batchBuffers;
                        std::array<std::size_t, 16> batchSizes;

                        std::size_t batchCount = 0;
                        std::size_t bytes = namedChannel.channel->receiveInto(rxBuffer, channelTimeout);
                        if (bytes > 0) {
                            if (bytes <= batchBuffers[0].size()) {
                                std::memcpy(batchBuffers[batchCount].data(), rxBuffer.data(), bytes);
                                batchSizes[batchCount++] = bytes;

                                constexpr IHandler::Timeout zeroTimeout { 0 };
                                while (batchCount < 16 && !stoken.stop_requested()) {
                                    std::size_t nextBytes = namedChannel.channel->receiveInto(rxBuffer, zeroTimeout);
                                    if (nextBytes == 0) {
                                        break;
                                    }
                                    if (nextBytes <= batchBuffers[0].size()) {
                                        std::memcpy(batchBuffers[batchCount].data(), rxBuffer.data(), nextBytes);
                                        batchSizes[batchCount++] = nextBytes;
                                    } else {
                                        std::array<std::span<const std::byte>, 16> batchSpans;
                                        for (std::size_t i = 0; i < batchCount; ++i) {
                                            batchSpans[i] = std::span<const std::byte>(batchBuffers[i].data(), batchSizes[i]);
                                        }
                                        handler.onBatchReceived(namedChannel.name, std::span<const std::span<const std::byte>>(batchSpans.data(), batchCount));
                                        batchCount = 0;
                                        handler.onMessageReceived(namedChannel.name, std::span<const std::byte>(rxBuffer.data(), nextBytes));
                                        break;
                                    }
                                }

                                if (batchCount > 0) {
                                    std::array<std::span<const std::byte>, 16> batchSpans;
                                    for (std::size_t i = 0; i < batchCount; ++i) {
                                        batchSpans[i] = std::span<const std::byte>(batchBuffers[i].data(), batchSizes[i]);
                                    }
                                    handler.onBatchReceived(namedChannel.name, std::span<const std::span<const std::byte>>(batchSpans.data(), batchCount));
                                }
                            } else {
                                handler.onMessageReceived(namedChannel.name, std::span<const std::byte>(rxBuffer.data(), bytes));
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
