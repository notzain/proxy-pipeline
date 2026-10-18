#include "PrintHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/Configuration.hpp"
#include "log/Log.hpp"
#include <filesystem>
#include <memory>

int main(int argc, char* argv[])
{
    std::filesystem::path configPath = "proxy.conf";
    if (argc > 1) {
        configPath = argv[1];
    }

    Proxy::Log::info("Starting PrintHandler executable...");
    Proxy::Log::info("Loading configuration from: {}", configPath.string());

    // Configuration is always read from file - applications never hardcode their own properties
    Core::Configuration config = Core::Configuration::fromFile(configPath);

    // Backend created via link-time resolution and configured purely from file (also initializes backend-specific logging)
    std::unique_ptr<::Backend::IBackend> backend = ::Backend::createBackend();
    backend->configure(config);

    Proxy::Handler::PrintHandler handler(std::move(backend));

    Proxy::Log::info("PrintHandler initialized successfully.");
    // Start the event loop (handles SIGINT / Ctrl+C to cleanly stop)
    handler.start();

    Proxy::Log::info("PrintHandler exiting cleanly.");
    return 0;
}
