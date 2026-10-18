#include "core/IHandler.hpp"
#include "core/IMessageReceiver.hpp"
#include "log/Log.hpp"
#include <csignal>
#include <thread>

namespace {
// Global pointer to active handler instance for C signal dispatching
std::atomic<Core::IHandler*> g_activeHandler { nullptr };

extern "C" void proxySignalHandler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM) {
        if (auto* handler = g_activeHandler.load(std::memory_order_acquire)) {
            handler->stop();
        }
    }
}
} // namespace

namespace Core {

IHandler::~IHandler()
{
    unregisterSignalHandler();
}

void IHandler::registerInputChannel(std::string_view channelName, std::unique_ptr<::Backend::IInputChannel> channel)
{
    if (!channel) {
        Proxy::Log::warn("[IHandler] Attempted to register null input channel '{}'", channelName);
        return;
    }
    m_inputChannels.push_back(NamedInputChannel { std::string(channelName), std::move(channel) });
}

void IHandler::step()
{
}

void IHandler::registerSignalHandler()
{
    g_activeHandler.store(this, std::memory_order_release);
    std::signal(SIGINT, proxySignalHandler);
    std::signal(SIGTERM, proxySignalHandler);
}

void IHandler::unregisterSignalHandler()
{
    Core::IHandler* expected = this;
    if (g_activeHandler.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) {
        std::signal(SIGINT, SIG_DFL);
        std::signal(SIGTERM, SIG_DFL);
    }
}

void IHandler::start()
{
    registerSignalHandler();
    m_running.store(true, std::memory_order_release);
    constexpr Timeout channelTimeout { 50 };

    auto* receiver = dynamic_cast<IMessageReceiver*>(this);

    Proxy::Log::info("[IHandler] Event loop started (channels: {}, listening for SIGINT/Ctrl+C)...", m_inputChannels.size());

    while (m_running.load(std::memory_order_acquire)) {
        if (!m_inputChannels.empty()) {
            for (auto& namedChannel : m_inputChannels) {
                if (!m_running.load(std::memory_order_acquire)) {
                    break;
                }
                if (namedChannel.channel) {
                    if (auto msg = namedChannel.channel->receive(channelTimeout); msg.has_value()) {
                        if (receiver) {
                            receiver->onMessageReceived(namedChannel.name, *msg);
                        }
                    }
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        step();
    }

    Proxy::Log::info("[IHandler] Event loop stopped.");
    unregisterSignalHandler();
}

void IHandler::stop()
{
    m_running.store(false, std::memory_order_release);
}

bool IHandler::isRunning() const noexcept
{
    return m_running.load(std::memory_order_acquire);
}

} // namespace Core
