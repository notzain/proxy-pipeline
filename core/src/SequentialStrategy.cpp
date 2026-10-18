#include "core/SequentialStrategy.hpp"
#include "core/IHandler.hpp"
#include "log/Log.hpp"
#include <chrono>
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

    while (m_running.load(std::memory_order_acquire)) {
        if (!channels.empty()) {
            for (const auto& namedChannel : channels) {
                if (!m_running.load(std::memory_order_acquire)) {
                    break;
                }
                if (namedChannel.channel) {
                    if (auto msg = namedChannel.channel->receive(channelTimeout); msg.has_value()) {
                        handler.onMessageReceived(namedChannel.name, *msg);
                    }
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        handler.step();
    }

    Proxy::Log::info("[SequentialStrategy] Loop stopped.");
}

void SequentialStrategy::stop()
{
    m_running.store(false, std::memory_order_release);
}

void SequentialStrategy::wait()
{
}

bool SequentialStrategy::isRunning() const noexcept
{
    return m_running.load(std::memory_order_acquire);
}

} // namespace Core
