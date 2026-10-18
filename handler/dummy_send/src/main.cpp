#include "DummySendHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/Configuration.hpp"
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

    Proxy::Log::info("Starting DummySend executable...");
    Proxy::Log::info("Loading configuration from: {}", configPath.string());

    Core::Configuration config = Core::Configuration::fromFile(configPath);

    std::unique_ptr<::Backend::IBackend> backend = ::Backend::createBackend();
    backend->configure(config);

    std::string channelName = config.getOr<std::string>("dummy.channel", "test_channel");
    std::string messageText = config.getOr<std::string>("dummy.message", "Hello from DummySend!");
    int delayMs = config.getOr<int>("dummy.delay_ms", 1000);

    auto outputChannel = backend->createOutputChannel(channelName, config);
    if (!outputChannel) {
        Proxy::Log::error("Failed to create output channel '{}'", channelName);
        return 1;
    }

    Proxy::Handler::DummySendHandler handler(channelName, messageText, std::chrono::milliseconds(delayMs));
    handler.registerOutputChannel(channelName, std::move(outputChannel));

    Proxy::Log::info("DummySend initialized (channel='{}', delay={}ms). Starting event loop...", channelName, delayMs);
    handler.start();

    Proxy::Log::info("DummySend exiting cleanly.");
    return 0;
}
