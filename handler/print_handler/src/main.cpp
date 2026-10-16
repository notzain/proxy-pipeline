#include "PrintHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/Configuration.hpp"
#include <filesystem>
#include <memory>
#include <spdlog/spdlog.h>

int main(int argc, char* argv[])
{
    std::filesystem::path configPath = "proxy.conf";
    if (argc > 1) {
        configPath = argv[1];
    }

    spdlog::info("Starting PrintHandler executable...");
    spdlog::info("Loading configuration from: {}", configPath.string());

    // Configuration is always read from file - applications never hardcode their own properties
    Core::Configuration config = Core::Configuration::fromFile(configPath);

    // Backend created via link-time resolution and configured purely from file
    std::unique_ptr<::Backend::IBackend> backend = ::Backend::createBackend();
    backend->configure(config);

    Proxy::Handler::PrintHandler handler(std::move(backend));

    spdlog::info("PrintHandler initialized successfully.");
    // Start the event loop (handles SIGINT / Ctrl+C to cleanly stop)
    handler.start();

    spdlog::info("PrintHandler exiting cleanly.");
    return 0;
}
