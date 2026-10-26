#include "OrderValidationHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "core/handler/HandlerRunner.hpp"
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

    Proxy::Handler::OrderValidationHandler handler(*backend);
    Core::Handler::HandlerRunner runner(handler);
    if (!runner.configure(config)) {
        return 1;
    }

    Proxy::Log::info("OrderValidationHandler initialized successfully. Starting event loop...");
    runner.start();

    Proxy::Log::info("OrderValidationHandler exiting cleanly.");
    return 0;
}
