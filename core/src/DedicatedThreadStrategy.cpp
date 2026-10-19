#include "core/DedicatedThreadStrategy.hpp"
#include "core/IHandler.hpp"
#include "log/Log.hpp"
#include <array>
#include <chrono>
#include <span>

namespace Core {

DedicatedThreadStrategy::DedicatedThreadStrategy() = default;

DedicatedThreadStrategy::~DedicatedThreadStrategy()
{
    stop();
    wait();
}

void DedicatedThreadStrategy::start(IHandler& handler)
{
    m_running.store(true, std::memory_order_release);
    constexpr IHandler::Timeout channelTimeout { 50 };

    const auto& channels = handler.getInputChannels();
    Proxy::Log::info("[DedicatedThreadStrategy] Launching dedicated threads for {} channel(s)...", channels.size());

    for (const auto& namedChannel : channels) {
        m_workerThreads.emplace_back([this, &handler, &namedChannel, channelTimeout](std::stop_token stoken) {
            Proxy::Log::debug("[DedicatedThreadStrategy] Poller thread started for channel '{}'", namedChannel.name);
            thread_local std::array<std::byte, 65535> rxBuffer;
            std::array<std::array<std::byte, 2048>, 16> batchBuffers;
            std::array<std::size_t, 16> batchSizes;

            while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
                if (namedChannel.channel) {
                    std::size_t batchCount = 0;
                    std::size_t bytes = namedChannel.channel->receiveInto(rxBuffer, channelTimeout);
                    if (bytes > 0) {
                        if (bytes <= batchBuffers[0].size()) {
                            std::memcpy(batchBuffers[batchCount].data(), rxBuffer.data(), bytes);
                            batchSizes[batchCount++] = bytes;

                            // Speculative drain: drain up to 16 packets already queued in the socket buffer
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
                                    for (std::size_t i = 0; i < batchCount; ++i) {
                                        handler.onMessageReceived(namedChannel.name, std::span<const std::byte>(batchBuffers[i].data(), batchSizes[i]));
                                    }
                                    batchCount = 0;
                                    handler.onMessageReceived(namedChannel.name, std::span<const std::byte>(rxBuffer.data(), nextBytes));
                                    break;
                                }
                            }

                            for (std::size_t i = 0; i < batchCount; ++i) {
                                handler.onMessageReceived(namedChannel.name, std::span<const std::byte>(batchBuffers[i].data(), batchSizes[i]));
                            }
                        } else {
                            handler.onMessageReceived(namedChannel.name, std::span<const std::byte>(rxBuffer.data(), bytes));
                        }
                    }
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

} // namespace Core
