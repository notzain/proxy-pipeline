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
            while (!stoken.stop_requested() && m_running.load(std::memory_order_acquire)) {
                if (namedChannel.channel) {
                    if (std::size_t bytes = namedChannel.channel->receiveInto(rxBuffer, channelTimeout); bytes > 0) {
                        handler.onMessageReceived(namedChannel.name, std::span<const std::byte>(rxBuffer.data(), bytes));

                        // Speculative drain: drain any additional packets already queued in the socket buffer without waiting
                        constexpr IHandler::Timeout zeroTimeout { 0 };
                        while (!stoken.stop_requested()) {
                            std::size_t nextBytes = namedChannel.channel->receiveInto(rxBuffer, zeroTimeout);
                            if (nextBytes == 0) {
                                break;
                            }
                            handler.onMessageReceived(namedChannel.name, std::span<const std::byte>(rxBuffer.data(), nextBytes));
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
