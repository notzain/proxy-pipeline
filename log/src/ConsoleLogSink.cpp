#include "log/ConsoleLogSink.hpp"
#include "SpdlogUtils.hpp"
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
