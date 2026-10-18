#include "XmlVerificationHandler.hpp"
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

    Proxy::Log::info("Starting XmlVerificationHandler executable...");
    Proxy::Log::info("Loading configuration from: {}", configPath.string());

    Core::Configuration config = Core::Configuration::fromFile(configPath);

    std::unique_ptr<::Backend::IBackend> backend = ::Backend::createBackend();
    backend->configure(config);

    std::string xsdPath = config.getOr<std::string>("xml.xsd_path", "");
    if (xsdPath.empty()) {
        Proxy::Log::error("[XmlVerificationHandler] Missing mandatory configuration: 'xml.xsd_path'");
        return 1;
    }

    std::string inChannelName = config.getOr<std::string>("xml.input_channel", config.getOr<std::string>("channel.input", "xml_in"));
    std::string outChannelName = config.getOr<std::string>("xml.output_channel", config.getOr<std::string>("channel.output", "xml_out"));

    auto inputChannel = backend->createInputChannel(inChannelName, config);
    if (!inputChannel) {
        Proxy::Log::error("[XmlVerificationHandler] Failed to create input channel '{}'", inChannelName);
        return 1;
    }

    auto outputChannel = backend->createOutputChannel(outChannelName, config);
    if (!outputChannel) {
        Proxy::Log::error("[XmlVerificationHandler] Failed to create output channel '{}'", outChannelName);
        return 1;
    }

    Proxy::Handler::XmlVerificationHandler handler(xsdPath, outChannelName);

    if (!handler.isSchemaLoaded()) {
        Proxy::Log::error("[XmlVerificationHandler] Failed to load XSD schema from '{}'. Terminating.", xsdPath);
        return 1;
    }

    handler.registerInputChannel(inChannelName, std::move(inputChannel));
    handler.registerOutputChannel(outChannelName, std::move(outputChannel));

    Proxy::Log::info("XmlVerificationHandler initialized (input='{}', output='{}', xsd='{}'). Starting event loop...",
        inChannelName, outChannelName, xsdPath);

    handler.start();

    Proxy::Log::info("XmlVerificationHandler exiting cleanly.");
    return 0;
}
