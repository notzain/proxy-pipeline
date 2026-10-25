#include "UdpEgressHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "log/Log.hpp"
#include <filesystem>
#include <memory>

int main(int argc, char* argv[])
{
    std::filesystem::path configPath = "proxy.conf";
    if (argc > 1) {
        configPath = argv[1];
    }

    Proxy::Log::info("Starting UdpEgressHandler executable...");
    Proxy::Log::info("Loading configuration from: {}", configPath.string());

    Core::Config::Configuration config = Core::Config::Configuration::fromFile(configPath);

    std::unique_ptr<::Backend::IBackend> backend = ::Backend::createBackend();
    backend->configure(config);

    Proxy::Handler::UdpEgressHandler handler(*backend);
    if (!handler.configure(config)) {
        return 1;
    }

    Proxy::Log::info("UdpEgressHandler initialized successfully. Starting event loop...");
    handler.start();

    Proxy::Log::info("UdpEgressHandler exiting cleanly.");
    return 0;
}
