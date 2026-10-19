#include "backend/file/FileBackend.hpp"
#include "backend/file/FileInputChannel.hpp"
#include "backend/file/FileOutputChannel.hpp"
#include "log/FileLogSink.hpp"
#include "log/Log.hpp"
#include <fmt/core.h>
#include <memory>

namespace Proxy::Backend::File {

void FileBackend::configure(const ::Backend::IConfig& config)
{
    m_watchDirectory = config.getOr<std::string>("file.path", "/tmp/watch");

    std::string logPath = config.getOr<std::string>("file.log", "proxy.log");
    bool logConsole = config.getOr<bool>("log.console", true);
    Proxy::Log::init(std::make_unique<Proxy::Log::FileLogSink>(logPath, logConsole));

    std::error_code ec;
    std::filesystem::create_directories(m_watchDirectory, ec);
    Proxy::Log::info("[FileBackend] Watching directory: {}", m_watchDirectory.string());
}

std::unique_ptr<::Backend::IInputChannel> FileBackend::createInputChannel(std::string_view name, const ::Backend::IConfig& config)
{
    std::string configKey = fmt::format("channel.{}.path", name);
    std::string defaultPath = fmt::format("/tmp/watch/{}", name);

    std::string dirPath = config.getOr<std::string>(configKey, config.getOr<std::string>("file.path", defaultPath));
    return std::make_unique<FileInputChannel>(std::filesystem::path(dirPath));
}

std::unique_ptr<::Backend::IOutputChannel> FileBackend::createOutputChannel(std::string_view name, const ::Backend::IConfig& config)
{
    std::string configKey = fmt::format("channel.{}.path", name);
    std::string defaultPath = fmt::format("/tmp/watch/{}", name);

    std::string dirPath = config.getOr<std::string>(configKey, config.getOr<std::string>("file.path", defaultPath));
    return std::make_unique<FileOutputChannel>(std::filesystem::path(dirPath));
}

} // namespace Proxy::Backend::File

namespace Backend {

[[gnu::weak]] std::unique_ptr<IBackend> createBackend()
{
    return std::make_unique<Proxy::Backend::File::FileBackend>();
}

} // namespace Backend
