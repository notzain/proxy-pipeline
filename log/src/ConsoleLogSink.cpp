#include "log/ConsoleLogSink.hpp"
#include "SpdlogUtils.hpp"
#include "log/Structured.hpp"
#include <fmt/format.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace Proxy::Log {

struct ConsoleLogSink::Impl {
    std::shared_ptr<spdlog::logger> logger;
};

ConsoleLogSink::ConsoleLogSink()
    : m_impl(std::make_unique<Impl>())
{
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    m_impl->logger = std::make_shared<spdlog::logger>("console", sink);
    m_impl->logger->set_level(spdlog::level::info);
    m_impl->logger->flush_on(spdlog::level::debug);
}

ConsoleLogSink::~ConsoleLogSink() = default;
ConsoleLogSink::ConsoleLogSink(ConsoleLogSink&&) noexcept = default;
ConsoleLogSink& ConsoleLogSink::operator=(ConsoleLogSink&&) noexcept = default;

void ConsoleLogSink::log(Level level, std::string_view message)
{
    m_impl->logger->log(Detail::toSpdlogLevel(level), "{}", message);
}

void ConsoleLogSink::logStructured(const StructuredLogRecord& record)
{
    fmt::memory_buffer buf;
    if (!record.component.empty()) {
        fmt::format_to(std::back_inserter(buf), "[{}] ", record.component);
    }
    buf.append(record.message);

    if (record.fields && record.fieldCount > 0) {
        buf.push_back(' ');
        buf.push_back('[');
        for (size_t i = 0; i < record.fieldCount; ++i) {
            const auto& field = record.fields[i];
            if (i > 0) {
                buf.push_back(' ');
            }
            fmt::format_to(std::back_inserter(buf), "{}=", field.key());
            std::visit([&buf](const auto& val) {
                using T = std::decay_t<decltype(val)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    buf.append(std::string_view("null"));
                } else if constexpr (std::is_same_v<T, bool>) {
                    buf.append(val ? std::string_view("true") : std::string_view("false"));
                } else if constexpr (std::is_arithmetic_v<T>) {
                    fmt::format_to(std::back_inserter(buf), "{}", val);
                } else if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>) {
                    buf.append(val);
                }
            },
                field.value());
        }
        buf.push_back(']');
    }

    m_impl->logger->log(Detail::toSpdlogLevel(record.level), "{}", std::string_view(buf.data(), buf.size()));
}

void ConsoleLogSink::flush()
{
    m_impl->logger->flush();
}

void ConsoleLogSink::setLevel(Level level)
{
    m_impl->logger->set_level(Detail::toSpdlogLevel(level));
}

Level ConsoleLogSink::getLevel() const
{
    return Detail::fromSpdlogLevel(m_impl->logger->level());
}

} // namespace Proxy::Log
