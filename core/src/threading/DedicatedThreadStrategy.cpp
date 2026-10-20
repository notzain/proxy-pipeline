#include "core/threading/DedicatedThreadStrategy.hpp"
#include "core/handler/IHandler.hpp"
#include "log/Log.hpp"
#include <array>
#include <chrono>
#include <span>

namespace Core::Threading {

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
            alignas(64) std::array<std::array<std::byte, 2048>, 16> batchBuffers;
            std::array<std::span<std::byte>, 16> destSpans;
            for (std::size_t i = 0; i < 16; ++i) {
                destSpans[i] = std::span<std::byte>(batchBuffers[i].data(), batchBuffers[i].size());
            }
            alignas(64) std::array<std::size_t, 16> batchSizes;

            while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
                if (namedChannel.channel) {
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
                            constexpr Handler::IHandler::Timeout zeroTimeout { 0 };
                            std::size_t jumboBytes = namedChannel.channel->receiveInto(jumboBuffer, zeroTimeout);
                            if (jumboBytes > 0) {
                                handler.onMessageReceived(namedChannel.name, std::span<const std::byte>(jumboBuffer.data(), jumboBytes));
                            }
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

} // namespace Core::Threading
