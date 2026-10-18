#include "MergeHandler.hpp"
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

    Proxy::Log::info("Starting MergeHandler executable...");
    Proxy::Log::info("Loading configuration from: {}", configPath.string());

    Core::Configuration config = Core::Configuration::fromFile(configPath);

    std::unique_ptr<::Backend::IBackend> backend = ::Backend::createBackend();
    backend->configure(config);

    Proxy::Handler::MergeHandler handler;
    if (!handler.configure(config, *backend)) {
        return 1;
    }

    Proxy::Log::info("MergeHandler initialized successfully. Starting event loop...");
    handler.start();

    Proxy::Log::info("MergeHandler exiting cleanly.");
    return 0;
}
