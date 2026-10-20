#include "OrderValidationHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "log/Log.hpp"
#include <filesystem>
#include <memory>
#include <string>

int main(int argc, char* argv[])
{
    std::filesystem::path configPath = "proxy.conf";
    if (argc > 1) {
        configPath = argv[1];
    }

    Proxy::Log::info("Starting OrderValidationHandler executable...");
    Proxy::Log::info("Loading configuration from: {}", configPath.string());

    Core::Config::Configuration config = Core::Config::Configuration::fromFile(configPath);

    std::unique_ptr<::Backend::IBackend> backend = ::Backend::createBackend();
    backend->configure(config);

    Proxy::Handler::OrderValidationHandler handler;
    if (!handler.configure(config, *backend)) {
        return 1;
    }

    Proxy::Log::info("OrderValidationHandler initialized successfully. Starting event loop...");
    handler.start();

    Proxy::Log::info("OrderValidationHandler exiting cleanly.");
    return 0;
}
