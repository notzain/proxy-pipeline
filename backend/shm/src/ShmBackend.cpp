#include "backend/shm/ShmBackend.hpp"
#include "backend/shm/ShmInputChannel.hpp"
#include "backend/shm/ShmOutputChannel.hpp"
#include "log/ConsoleLogSink.hpp"
#include "log/Log.hpp"
#include <fmt/core.h>
#include <memory>
#include <utility>

namespace Proxy::Backend::Shm {

ShmBackend::ShmBackend() = default;

ShmBackend::~ShmBackend() = default;

void ShmBackend::configure(const ::Backend::IConfig& config)
{
    m_shmPath = config.getOr<std::string>("shm.path", "/tmp/proxy_shm.dat");
    m_capacity = static_cast<std::size_t>(config.getOr<int>("shm.capacity", 1024));
    Proxy::Log::init(std::make_unique<Proxy::Log::ConsoleLogSink>());
}

std::unique_ptr<::Backend::IInputChannel> ShmBackend::createInputChannel(std::string_view name, const ::Backend::IConfig& config)
{
    std::string key = fmt::format("channel.{}.path", name);
    std::string defaultPath = fmt::format("/tmp/proxy_shm_{}.dat", name);
    std::string path = config.getOr<std::string>(key, config.getOr<std::string>("shm.path", defaultPath));
    std::size_t capacity = static_cast<std::size_t>(config.getOr<int>(fmt::format("channel.{}.capacity", name), static_cast<int>(m_capacity)));

    return std::make_unique<ShmInputChannel>(std::string(name), std::move(path), capacity);
}

std::unique_ptr<::Backend::IOutputChannel> ShmBackend::createOutputChannel(std::string_view name, const ::Backend::IConfig& config)
{
    std::string key = fmt::format("channel.{}.dest_path", name);
    std::string fallbackKey = fmt::format("channel.{}.path", name);
    std::string defaultPath = fmt::format("/tmp/proxy_shm_{}.dat", name);

    std::string path = config.getOr<std::string>(
        key,
        config.getOr<std::string>(
            fallbackKey,
            config.getOr<std::string>("shm.dest_path", config.getOr<std::string>("shm.path", defaultPath))));
    std::size_t capacity = static_cast<std::size_t>(config.getOr<int>(fmt::format("channel.{}.capacity", name), static_cast<int>(m_capacity)));

    return std::make_unique<ShmOutputChannel>(std::string(name), std::move(path), capacity);
}

} // namespace Proxy::Backend::Shm

namespace Backend {

[[gnu::weak]] std::unique_ptr<IBackend> createBackend()
{
    return std::make_unique<Proxy::Backend::Shm::ShmBackend>();
}

} // namespace Backend
