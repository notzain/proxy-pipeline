#include "DummyReceiveHandler.hpp"
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

    PROXY_LOG_INFO("Starting DummyReceive executable...");
    PROXY_LOG_INFO("Loading configuration from: {}", configPath.string());

    Core::Config::Configuration config = Core::Config::Configuration::fromFile(configPath);

    std::unique_ptr<::Backend::IBackend> backend = ::Backend::createBackend();
    backend->configure(config);

    Proxy::Handler::DummyReceiveHandler handler(*backend);
    Core::Handler::HandlerRunner runner(handler);
    if (!runner.configure(config)) {
        return 1;
    }

    PROXY_LOG_INFO("DummyReceive initialized successfully. Starting event loop...");
    runner.start();

    PROXY_LOG_INFO("DummyReceive exiting cleanly.");
    return 0;
}
