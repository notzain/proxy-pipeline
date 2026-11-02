#include "core/handler/PosixSignalManager.hpp"
#include "log/Log.hpp"
#include <atomic>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <system_error>

namespace Core::Handler {

namespace {

    static_assert(std::atomic<bool>::is_always_lock_free);

    std::atomic<bool> g_shutdownRequested { false };
    std::mutex g_signalOwnershipMutex;
    const void* g_activeOwner { nullptr };
    struct sigaction g_previousSigintAction { };
    struct sigaction g_previousSigtermAction { };

    extern "C" void proxySignalHandler(int signal)
    {
        if (signal == SIGINT || signal == SIGTERM) {
            g_shutdownRequested.store(true, std::memory_order_relaxed);
        }
    }

} // namespace

void PosixSignalManager::acquire(const void* owner)
{
    std::lock_guard lock(g_signalOwnershipMutex);
    if (g_activeOwner) {
        throw std::logic_error("Signal handling is already owned by another HandlerRunner");
    }

    g_shutdownRequested.store(false, std::memory_order_relaxed);

    struct sigaction action { };
    action.sa_handler = proxySignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGINT, &action, &g_previousSigintAction) != 0) {
        throw std::system_error(errno, std::generic_category(), "Failed to register SIGINT handler");
    }

    if (sigaction(SIGTERM, &action, &g_previousSigtermAction) != 0) {
        const int error = errno;
        if (sigaction(SIGINT, &g_previousSigintAction, nullptr) != 0) {
            Proxy::Log::error("[PosixSignalManager] Failed to restore previous SIGINT handler: {}", std::strerror(errno));
        }
        throw std::system_error(error, std::generic_category(), "Failed to register SIGTERM handler");
    }

    g_activeOwner = owner;
}

void PosixSignalManager::release(const void* owner) noexcept
{
    std::lock_guard lock(g_signalOwnershipMutex);
    if (g_activeOwner != owner) {
        return;
    }

    if (sigaction(SIGINT, &g_previousSigintAction, nullptr) != 0) {
        Proxy::Log::error("[PosixSignalManager] Failed to restore previous SIGINT handler: {}", std::strerror(errno));
    }
    if (sigaction(SIGTERM, &g_previousSigtermAction, nullptr) != 0) {
        Proxy::Log::error("[PosixSignalManager] Failed to restore previous SIGTERM handler: {}", std::strerror(errno));
    }

    g_activeOwner = nullptr;
    g_shutdownRequested.store(false, std::memory_order_relaxed);
}

bool PosixSignalManager::isShutdownRequested() noexcept
{
    return g_shutdownRequested.load(std::memory_order_relaxed);
}

void PosixSignalManager::reset() noexcept
{
    g_shutdownRequested.store(false, std::memory_order_relaxed);
}

} // namespace Core::Handler

