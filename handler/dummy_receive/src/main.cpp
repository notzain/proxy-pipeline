#include "DummyReceiveHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/Configuration.hpp"
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

    Proxy::Log::info("Starting DummyReceive executable...");
    Proxy::Log::info("Loading configuration from: {}", configPath.string());

    Core::Configuration config = Core::Configuration::fromFile(configPath);

    std::unique_ptr<::Backend::IBackend> backend = ::Backend::createBackend();
    backend->configure(config);

    std::string channelName = config.getOr<std::string>("dummy.channel", "test_channel");

    auto inputChannel = backend->createInputChannel(channelName, config);
    if (!inputChannel) {
        Proxy::Log::error("Failed to create input channel '{}'", channelName);
        return 1;
    }

    Proxy::Handler::DummyReceiveHandler handler;
    handler.registerInputChannel(channelName, std::move(inputChannel));

    Proxy::Log::info("DummyReceive initialized (channel='{}'). Starting event loop...", channelName);
    handler.start();

    Proxy::Log::info("DummyReceive exiting cleanly.");
    return 0;
}
