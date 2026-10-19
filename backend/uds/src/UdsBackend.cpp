#include "backend/uds/UdsBackend.hpp"
#include "backend/uds/UdsInputChannel.hpp"
#include "backend/uds/UdsOutputChannel.hpp"
#include "log/ConsoleLogSink.hpp"
#include "log/Log.hpp"
#include <fmt/core.h>
#include <memory>
#include <utility>

namespace Proxy::Backend::Uds {

UdsBackend::UdsBackend() = default;

UdsBackend::~UdsBackend() = default;

void UdsBackend::configure(const ::Backend::IConfig& config)
{
    m_socketPath = config.getOr<std::string>("uds.path", "/tmp/proxy_uds.sock");
    Proxy::Log::init(std::make_unique<Proxy::Log::ConsoleLogSink>());
}

std::unique_ptr<::Backend::IInputChannel> UdsBackend::createInputChannel(std::string_view name, const ::Backend::IConfig& config)
{
    std::string key = fmt::format("channel.{}.path", name);
    std::string defaultPath = fmt::format("/tmp/proxy_uds_{}.sock", name);
    std::string path = config.getOr<std::string>(key, config.getOr<std::string>("uds.path", defaultPath));

    return std::make_unique<UdsInputChannel>(std::move(path));
}

std::unique_ptr<::Backend::IOutputChannel> UdsBackend::createOutputChannel(std::string_view name, const ::Backend::IConfig& config)
{
    std::string key = fmt::format("channel.{}.dest_path", name);
    std::string fallbackKey = fmt::format("channel.{}.path", name);
    std::string defaultPath = fmt::format("/tmp/proxy_uds_{}.sock", name);

    std::string path = config.getOr<std::string>(
        key,
        config.getOr<std::string>(
            fallbackKey,
            config.getOr<std::string>("uds.dest_path", config.getOr<std::string>("uds.path", defaultPath))));

    return std::make_unique<UdsOutputChannel>(std::move(path));
}

} // namespace Proxy::Backend::Uds

namespace Backend {

[[gnu::weak]] std::unique_ptr<IBackend> createBackend()
{
    return std::make_unique<Proxy::Backend::Uds::UdsBackend>();
}

} // namespace Backend
