#include "backend/socket/SocketBackend.hpp"
#include "backend/socket/SocketInputChannel.hpp"
#include "backend/socket/SocketOutputChannel.hpp"
#include "log/Log.hpp"
#include "log/SocketLogSink.hpp"
#include <fmt/core.h>
#include <memory>
#include <utility>

namespace Proxy::Backend::Socket {

SocketBackend::SocketBackend() = default;

SocketBackend::~SocketBackend() = default;

void SocketBackend::configure(const ::Backend::IConfig& config)
{
    m_host = config.getOr<std::string>("socket.host", "0.0.0.0");
    m_port = config.getOr<std::uint16_t>("socket.port", 8080);

    std::string logHost = config.getOr<std::string>("socket.log_host", m_host == "0.0.0.0" ? "127.0.0.1" : m_host);
    std::uint16_t logPort = config.getOr<std::uint16_t>("socket.log_port", 9090);
    bool logConsole = config.getOr<bool>("log.console", true);
    Proxy::Log::init(std::make_unique<Proxy::Log::SocketLogSink>(logHost, logPort, logConsole));
}

std::unique_ptr<::Backend::IInputChannel> SocketBackend::createInputChannel(std::string_view name, const ::Backend::IConfig& config)
{
    std::string hostKey = fmt::format("channel.{}.host", name);
    std::string portKey = fmt::format("channel.{}.port", name);

    std::string host = config.getOr<std::string>(hostKey, config.getOr<std::string>("socket.host", "0.0.0.0"));
    std::uint16_t port = config.getOr<std::uint16_t>(portKey, config.getOr<std::uint16_t>("socket.port", 8080));

    return std::make_unique<SocketInputChannel>(std::string(name), std::move(host), port);
}

std::unique_ptr<::Backend::IOutputChannel> SocketBackend::createOutputChannel(std::string_view name, const ::Backend::IConfig& config)
{
    std::string hostKey = fmt::format("channel.{}.dest_host", name);
    std::string portKey = fmt::format("channel.{}.dest_port", name);

    std::string host = config.getOr<std::string>(hostKey, config.getOr<std::string>("socket.dest_host", "127.0.0.1"));
    std::uint16_t port = config.getOr<std::uint16_t>(portKey, config.getOr<std::uint16_t>("socket.dest_port", 8080));

    return std::make_unique<SocketOutputChannel>(std::string(name), std::move(host), port);
}

} // namespace Proxy::Backend::Socket

namespace Backend {

[[gnu::weak]] std::unique_ptr<IBackend> createBackend()
{
    return std::make_unique<Proxy::Backend::Socket::SocketBackend>();
}

} // namespace Backend
