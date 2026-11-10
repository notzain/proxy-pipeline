#include "DummySendHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "core/handler/HandlerRunner.hpp"
#include "log/Log.hpp"
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

int main(int argc, char* argv[])
{
    std::filesystem::path configPath = "proxy.conf";
    if (argc > 1) {
        configPath = argv[1];
    }

    PROXY_LOG_INFO("Starting DummySend executable...");
    PROXY_LOG_INFO("Loading configuration from: {}", configPath.string());

    Core::Config::Configuration config = Core::Config::Configuration::fromFile(configPath);

    std::unique_ptr<::Backend::IBackend> backend = ::Backend::createBackend();
    backend->configure(config);

    Proxy::Handler::DummySendHandler handler(*backend);
    Core::Handler::HandlerRunner runner(handler);
    if (!runner.configure(config)) {
        return 1;
    }

    PROXY_LOG_INFO("DummySend initialized successfully. Starting event loop...");
    runner.start();

    PROXY_LOG_INFO("DummySend exiting cleanly.");
    return 0;
}
