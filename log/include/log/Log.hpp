#pragma once

#include "log/ILogSink.hpp"
#include <fmt/core.h>
#include <memory>
#include <string_view>

namespace Proxy::Log {

/**
 * @brief Initializes the universal logging library with a concrete sink interface.
 *
 * @param sink Concrete ILogSink implementation. If nullptr, defaults to ConsoleLogSink.
 */
void init(std::unique_ptr<ILogSink> sink);

/**
 * @brief Sets the global logging severity threshold.
 *
 * @param level Minimum severity level required for messages to be logged.
 */
void setLevel(Level level);

/**
 * @brief Retrieves the current global logging severity threshold.
 *
 * @return Level Current severity level.
 */
[[nodiscard]] Level getLevel();

/**
 * @brief Flushes all log sinks immediately.
 */
void flush();

/**
 * @brief Core logging function emitting an already-formatted message at the specified severity.
 *
 * @param level Severity level of the log message.
 * @param message Pre-formatted message string view.
 */
void log(Level level, std::string_view message);

/**
 * @brief Logs a raw string message at Trace level.
 */
void trace(std::string_view message);

/**
 * @brief Logs a raw string message at Debug level.
 */
void debug(std::string_view message);

/**
 * @brief Logs a raw string message at Info level.
 */
void info(std::string_view message);

/**
 * @brief Logs a raw string message at Warn level.
 */
void warn(std::string_view message);

/**
 * @brief Logs a raw string message at Error level.
 */
void error(std::string_view message);

/**
 * @brief Logs a raw string message at Critical level.
 */
void critical(std::string_view message);

/**
 * @brief Logs a formatted message at Trace level using fmt formatting.
 */
template <typename... Args>
    requires(sizeof...(Args) > 0)
void trace(fmt::format_string<Args...> fmtStr, Args&&... args)
{
    log(Level::Trace, fmt::format(fmtStr, std::forward<Args>(args)...));
}

/**
 * @brief Logs a formatted message at Debug level using fmt formatting.
 */
template <typename... Args>
    requires(sizeof...(Args) > 0)
void debug(fmt::format_string<Args...> fmtStr, Args&&... args)
{
    log(Level::Debug, fmt::format(fmtStr, std::forward<Args>(args)...));
}

/**
 * @brief Logs a formatted message at Info level using fmt formatting.
 */
template <typename... Args>
    requires(sizeof...(Args) > 0)
void info(fmt::format_string<Args...> fmtStr, Args&&... args)
{
    log(Level::Info, fmt::format(fmtStr, std::forward<Args>(args)...));
}

/**
 * @brief Logs a formatted message at Warn level using fmt formatting.
 */
template <typename... Args>
    requires(sizeof...(Args) > 0)
void warn(fmt::format_string<Args...> fmtStr, Args&&... args)
{
    log(Level::Warn, fmt::format(fmtStr, std::forward<Args>(args)...));
}

/**
 * @brief Logs a formatted message at Error level using fmt formatting.
 */
template <typename... Args>
    requires(sizeof...(Args) > 0)
void error(fmt::format_string<Args...> fmtStr, Args&&... args)
{
    log(Level::Error, fmt::format(fmtStr, std::forward<Args>(args)...));
}

/**
 * @brief Logs a formatted message at Critical level using fmt formatting.
 */
template <typename... Args>
    requires(sizeof...(Args) > 0)
void critical(fmt::format_string<Args...> fmtStr, Args&&... args)
{
    log(Level::Critical, fmt::format(fmtStr, std::forward<Args>(args)...));
}

} // namespace Proxy::Log

namespace ProxyLog = Proxy::Log;
