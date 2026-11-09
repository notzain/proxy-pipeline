#pragma once

#include "log/ILogSink.hpp"
#include <memory>
#include <string>

namespace Proxy::Log {

/**
 * @brief Logging sink formatting output as Newline-Delimited JSON (NDJSON).
 */
class JsonLogSink : public ILogSink {
public:
    /**
     * @brief Constructs a JsonLogSink outputting to stdout or stderr.
     *
     * @param toStderr If true, outputs to stderr; otherwise stdout.
     */
    explicit JsonLogSink(bool toStderr = false);

    /**
     * @brief Constructs a JsonLogSink outputting to a file.
     *
     * @param filepath Path to the target log file.
     * @param mirrorToConsole If true, also writes JSON lines to stdout.
     */
    explicit JsonLogSink(const std::string& filepath, bool mirrorToConsole = false);

    ~JsonLogSink() override;
    JsonLogSink(JsonLogSink&&) noexcept;
    JsonLogSink& operator=(JsonLogSink&&) noexcept;

    JsonLogSink(const JsonLogSink&) = delete;
    JsonLogSink& operator=(const JsonLogSink&) = delete;

    void log(Level level, std::string_view message) override;
    void logStructured(const StructuredLogRecord& record) override;
    void flush() override;
    void setLevel(Level level) override;
    [[nodiscard]] Level getLevel() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Proxy::Log
