#include "log/JsonLogSink.hpp"
#include "SpdlogUtils.hpp"
#include "log/Structured.hpp"
#include <chrono>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace Proxy::Log {

namespace {

    constexpr std::string_view levelToString(Level level) noexcept
    {
        switch (level) {
        case Level::Trace:
            return "TRACE";
        case Level::Debug:
            return "DEBUG";
        case Level::Info:
            return "INFO";
        case Level::Warn:
            return "WARN";
        case Level::Error:
            return "ERROR";
        case Level::Critical:
            return "CRITICAL";
        case Level::Off:
            return "OFF";
        }
        return "INFO";
    }

    void escapeJsonStringTo(fmt::memory_buffer& out, std::string_view s)
    {
        for (char c : s) {
            switch (c) {
            case '"':
                out.append(std::string_view("\\\""));
                break;
            case '\\':
                out.append(std::string_view("\\\\"));
                break;
            case '\b':
                out.append(std::string_view("\\b"));
                break;
            case '\f':
                out.append(std::string_view("\\f"));
                break;
            case '\n':
                out.append(std::string_view("\\n"));
                break;
            case '\r':
                out.append(std::string_view("\\r"));
                break;
            case '\t':
                out.append(std::string_view("\\t"));
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    fmt::format_to(std::back_inserter(out), "\\u{:04x}", static_cast<unsigned int>(c));
                } else {
                    out.push_back(c);
                }
                break;
            }
        }
    }

} // namespace

struct JsonLogSink::Impl {
    std::shared_ptr<spdlog::logger> logger;
};

JsonLogSink::JsonLogSink(bool toStderr)
    : m_impl(std::make_unique<Impl>())
{
    std::shared_ptr<spdlog::sinks::sink> sink;
    if (toStderr) {
        sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    } else {
        sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    }
    m_impl->logger = std::make_shared<spdlog::logger>("json_console", sink);
    m_impl->logger->set_pattern("%v");
    m_impl->logger->set_level(spdlog::level::info);
    m_impl->logger->flush_on(spdlog::level::debug);
}

JsonLogSink::JsonLogSink(const std::string& filepath, bool mirrorToConsole)
    : m_impl(std::make_unique<Impl>())
{
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(filepath, true));
    if (mirrorToConsole) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }
    m_impl->logger = std::make_shared<spdlog::logger>("json_file", sinks.begin(), sinks.end());
    m_impl->logger->set_pattern("%v");
    m_impl->logger->set_level(spdlog::level::info);
    m_impl->logger->flush_on(spdlog::level::debug);
}

JsonLogSink::~JsonLogSink() = default;
JsonLogSink::JsonLogSink(JsonLogSink&&) noexcept = default;
JsonLogSink& JsonLogSink::operator=(JsonLogSink&&) noexcept = default;

void JsonLogSink::log(Level level, std::string_view message)
{
    StructuredLogRecord record;
    record.level = level;
    record.component = "";
    record.message = message;
    logStructured(record);
}

void JsonLogSink::logStructured(const StructuredLogRecord& record)
{
    const auto now = std::chrono::system_clock::now();
    fmt::memory_buffer buf;

    // ISO8601 UTC timestamp format: YYYY-MM-DDTHH:MM:SS.mmmZ
    fmt::format_to(std::back_inserter(buf),
        "{{\"timestamp\":\"{:%Y-%m-%dT%H:%M:%S}.{:03d}Z\",\"level\":\"{}\"",
        now,
        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000),
        levelToString(record.level));

    if (!record.component.empty()) {
        buf.append(std::string_view(",\"component\":\""));
        escapeJsonStringTo(buf, record.component);
        buf.push_back('"');
    }

    buf.append(std::string_view(",\"message\":\""));
    escapeJsonStringTo(buf, record.message);
    buf.push_back('"');

    if (record.fields && record.fieldCount > 0) {
        for (size_t i = 0; i < record.fieldCount; ++i) {
            const auto& field = record.fields[i];
            if (field.key().empty()) {
                continue;
            }
            buf.append(std::string_view(",\""));
            escapeJsonStringTo(buf, field.key());
            buf.append(std::string_view("\":"));

            if (field.isRawJson()) {
                std::visit([&buf](const auto& val) {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>) {
                        buf.append(std::string_view(val));
                    }
                },
                    field.value());
            } else {
                std::visit([&buf](const auto& val) {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, std::monostate>) {
                        buf.append(std::string_view("null"));
                    } else if constexpr (std::is_same_v<T, bool>) {
                        buf.append(val ? std::string_view("true") : std::string_view("false"));
                    } else if constexpr (std::is_arithmetic_v<T>) {
                        fmt::format_to(std::back_inserter(buf), "{}", val);
                    } else if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>) {
                        buf.push_back('"');
                        escapeJsonStringTo(buf, val);
                        buf.push_back('"');
                    }
                },
                    field.value());
            }
        }
    }

    buf.push_back('}');
    m_impl->logger->log(Detail::toSpdlogLevel(record.level), "{}", std::string_view(buf.data(), buf.size()));
}

void JsonLogSink::flush()
{
    m_impl->logger->flush();
}

void JsonLogSink::setLevel(Level level)
{
    m_impl->logger->set_level(Detail::toSpdlogLevel(level));
}

Level JsonLogSink::getLevel() const
{
    return Detail::fromSpdlogLevel(m_impl->logger->level());
}

} // namespace Proxy::Log
