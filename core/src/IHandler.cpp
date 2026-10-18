#include "core/IHandler.hpp"
#include "log/Log.hpp"
#include <csignal>

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
    constexpr Timeout defaultTimeout { 100 };

    Proxy::Log::info("[IHandler] Event loop started (listening for SIGINT/Ctrl+C)...");

    while (m_running.load(std::memory_order_acquire)) {
        if (auto message = nextMessage(defaultTimeout); message.has_value()) {
            onMessageReceived(*message);
        }
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
