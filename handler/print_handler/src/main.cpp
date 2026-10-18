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

    std::string channelName = config.getOr<std::string>("print.channel", "default");
    auto inputChannel = backend->createInputChannel(channelName, config);
    if (!inputChannel) {
        Proxy::Log::error("Failed to create input channel '{}'", channelName);
        return 1;
    }

    Proxy::Handler::PrintHandler handler;
    handler.registerInputChannel(channelName, std::move(inputChannel));

    Proxy::Log::info("PrintHandler initialized successfully (channel='{}').", channelName);
    // Start the event loop (handles SIGINT / Ctrl+C to cleanly stop)
    handler.start();

    Proxy::Log::info("PrintHandler exiting cleanly.");
    return 0;
}
