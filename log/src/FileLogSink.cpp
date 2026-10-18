#include "log/FileLogSink.hpp"
#include "SpdlogUtils.hpp"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <vector>

namespace Proxy::Log {

struct FileLogSink::Impl {
    std::shared_ptr<spdlog::logger> logger;
};

FileLogSink::FileLogSink(const std::filesystem::path& logPath, bool alsoLogToConsole)
    : m_impl(std::make_unique<Impl>())
{
    std::vector<spdlog::sink_ptr> sinks;
    if (alsoLogToConsole) {
        sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }

    if (logPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(logPath.parent_path(), ec);
    }

    sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), false));

    m_impl->logger = std::make_shared<spdlog::logger>("file", sinks.begin(), sinks.end());
    m_impl->logger->set_level(spdlog::level::info);
    m_impl->logger->flush_on(spdlog::level::debug);
}

FileLogSink::~FileLogSink() = default;
FileLogSink::FileLogSink(FileLogSink&&) noexcept = default;
FileLogSink& FileLogSink::operator=(FileLogSink&&) noexcept = default;

void FileLogSink::log(Level level, std::string_view message)
{
    m_impl->logger->log(Detail::toSpdlogLevel(level), "{}", message);
}

void FileLogSink::flush()
{
    m_impl->logger->flush();
}

void FileLogSink::setLevel(Level level)
{
    m_impl->logger->set_level(Detail::toSpdlogLevel(level));
}

Level FileLogSink::getLevel() const
{
    return Detail::fromSpdlogLevel(m_impl->logger->level());
}

} // namespace Proxy::Log
