#include "log/Log.hpp"
#include "log/ConsoleLogSink.hpp"
#include <atomic>
#include <mutex>

namespace {

std::mutex g_loggerMutex;
std::unique_ptr<Proxy::Log::ILogSink> g_currentSink;

/// Sentinel stored in g_cachedLevel until the active sink's level has been read once.
constexpr auto LevelUnknown = static_cast<std::uint8_t>(0xFF);

/// Lock-free mirror of the active sink's level, consulted by shouldLog() on hot paths.
std::atomic<std::uint8_t> g_cachedLevel { LevelUnknown };

Proxy::Log::ILogSink& getActiveSink()
{
    if (!g_currentSink) {
        g_currentSink = std::make_unique<Proxy::Log::ConsoleLogSink>();
    }
    return *g_currentSink;
}

/// Refreshes the cached level from the active sink. Caller must hold g_loggerMutex.
void refreshCachedLevel()
{
    g_cachedLevel.store(static_cast<std::uint8_t>(getActiveSink().getLevel()), std::memory_order_relaxed);
}

} // namespace

namespace Proxy::Log {

void init(std::unique_ptr<ILogSink> sink)
{
    std::lock_guard<std::mutex> lock(g_loggerMutex);
    Level preservedLevel = g_currentSink ? g_currentSink->getLevel() : Level::Info;
    if (!sink) {
        sink = std::make_unique<ConsoleLogSink>();
    }
    sink->setLevel(preservedLevel);
    g_currentSink = std::move(sink);
    refreshCachedLevel();
}

void setLevel(Level level)
{
    std::lock_guard<std::mutex> lock(g_loggerMutex);
    getActiveSink().setLevel(level);
    refreshCachedLevel();
}

Level getLevel()
{
    std::lock_guard<std::mutex> lock(g_loggerMutex);
    return getActiveSink().getLevel();
}

bool shouldLog(Level level) noexcept
{
    std::uint8_t cached = g_cachedLevel.load(std::memory_order_relaxed);
    if (cached == LevelUnknown) [[unlikely]] {
        std::lock_guard<std::mutex> lock(g_loggerMutex);
        refreshCachedLevel();
        cached = g_cachedLevel.load(std::memory_order_relaxed);
    }
    return static_cast<std::uint8_t>(level) >= cached;
}

void flush()
{
    std::lock_guard<std::mutex> lock(g_loggerMutex);
    if (g_currentSink) {
        g_currentSink->flush();
    }
}

void log(Level level, std::string_view message)
{
    if (!shouldLog(level)) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_loggerMutex);
    getActiveSink().log(level, message);
}

void trace(std::string_view message)
{
    log(Level::Trace, message);
}

void debug(std::string_view message)
{
    log(Level::Debug, message);
}

void info(std::string_view message)
{
    log(Level::Info, message);
}

void warn(std::string_view message)
{
    log(Level::Warn, message);
}

void error(std::string_view message)
{
    log(Level::Error, message);
}

void critical(std::string_view message)
{
    log(Level::Critical, message);
}

} // namespace Proxy::Log
