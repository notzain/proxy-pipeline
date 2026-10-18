#pragma once

#include <cstdint>
#include <string_view>

namespace Proxy::Log {

/**
 * @brief Severity level for log messages.
 */
enum class Level : std::uint8_t {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
    Off
};

/**
 * @brief Abstract interface representing a logging sink destination.
 */
class ILogSink {
public:
    virtual ~ILogSink() = default;

    /**
     * @brief Emits a log message with the specified severity level.
     *
     * @param level Severity level of the log message.
     * @param message Pre-formatted message string view.
     */
    virtual void log(Level level, std::string_view message) = 0;

    /**
     * @brief Flushes buffered log messages to the underlying destination.
     */
    virtual void flush() = 0;

    /**
     * @brief Sets the logging severity threshold for this sink.
     *
     * @param level Minimum severity level required for messages to be logged.
     */
    virtual void setLevel(Level level) = 0;

    /**
     * @brief Gets the current logging severity threshold of this sink.
     *
     * @return Level Current severity level.
     */
    [[nodiscard]] virtual Level getLevel() const = 0;
};

} // namespace Proxy::Log
