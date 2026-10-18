#include "OrderValidationHandler.hpp"
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

    Proxy::Log::info("Starting OrderValidationHandler executable...");
    Proxy::Log::info("Loading configuration from: {}", configPath.string());

    Core::Configuration config = Core::Configuration::fromFile(configPath);

    std::unique_ptr<::Backend::IBackend> backend = ::Backend::createBackend();
    backend->configure(config);

    std::string inChannelName = config.getOr<std::string>("order.input_channel", config.getOr<std::string>("channel.input", "order_in"));
    std::string outChannelName = config.getOr<std::string>("order.output_channel", config.getOr<std::string>("channel.output", "order_out"));

    auto inputChannel = backend->createInputChannel(inChannelName, config);
    if (!inputChannel) {
        Proxy::Log::error("[OrderValidationHandler] Failed to create input channel '{}'", inChannelName);
        return 1;
    }

    auto outputChannel = backend->createOutputChannel(outChannelName, config);
    if (!outputChannel) {
        Proxy::Log::error("[OrderValidationHandler] Failed to create output channel '{}'", outChannelName);
        return 1;
    }

    Proxy::Handler::OrderValidationHandler handler(outChannelName);

    handler.registerInputChannel(inChannelName, std::move(inputChannel));
    handler.registerOutputChannel(outChannelName, std::move(outputChannel));

    Proxy::Log::info("OrderValidationHandler initialized (input='{}', output='{}'). Starting event loop...",
        inChannelName, outChannelName);

    handler.start();

    Proxy::Log::info("OrderValidationHandler exiting cleanly.");
    return 0;
}

