#include "log/Log.hpp"
#include "log/ConsoleLogSink.hpp"
#include <mutex>

namespace {

std::mutex g_loggerMutex;
std::unique_ptr<Proxy::Log::ILogSink> g_currentSink;

Proxy::Log::ILogSink& getActiveSink()
{
    if (!g_currentSink) {
        g_currentSink = std::make_unique<Proxy::Log::ConsoleLogSink>();
    }
    return *g_currentSink;
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
}

void setLevel(Level level)
{
    std::lock_guard<std::mutex> lock(g_loggerMutex);
    getActiveSink().setLevel(level);
}

Level getLevel()
{
    std::lock_guard<std::mutex> lock(g_loggerMutex);
    return getActiveSink().getLevel();
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
