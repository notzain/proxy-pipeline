#include "log/SocketLogSink.hpp"
#include "SpdlogUtils.hpp"
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/udp_sink.h>
#include <spdlog/spdlog.h>
#include <vector>

namespace Proxy::Log {

struct SocketLogSink::Impl {
    std::shared_ptr<spdlog::logger> logger;
};

SocketLogSink::SocketLogSink(std::string host, std::uint16_t port, bool alsoLogToConsole)
    : m_impl(std::make_unique<Impl>())
{
    std::vector<spdlog::sink_ptr> sinks;
    if (alsoLogToConsole) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    spdlog::sinks::udp_sink_config udpConfig(std::move(host), port);
    sinks.push_back(std::make_shared<spdlog::sinks::udp_sink_mt>(udpConfig));

    m_impl->logger = std::make_shared<spdlog::logger>("socket", sinks.begin(), sinks.end());
    m_impl->logger->set_level(spdlog::level::info);
    m_impl->logger->flush_on(spdlog::level::debug);
}

SocketLogSink::~SocketLogSink() = default;
SocketLogSink::SocketLogSink(SocketLogSink&&) noexcept = default;
SocketLogSink& SocketLogSink::operator=(SocketLogSink&&) noexcept = default;

void SocketLogSink::log(Level level, std::string_view message)
{
    m_impl->logger->log(Detail::toSpdlogLevel(level), "{}", message);
}

void SocketLogSink::flush()
{
    m_impl->logger->flush();
}

void SocketLogSink::setLevel(Level level)
{
    m_impl->logger->set_level(Detail::toSpdlogLevel(level));
}

Level SocketLogSink::getLevel() const
{
    return Detail::fromSpdlogLevel(m_impl->logger->level());
}

} // namespace Proxy::Log
