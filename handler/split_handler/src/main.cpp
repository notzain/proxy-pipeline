#include "SplitHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/Configuration.hpp"
#include "log/Log.hpp"
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::vector<std::string> splitString(std::string_view str, char delimiter)
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream { std::string(str) };
    while (std::getline(tokenStream, token, delimiter)) {
        auto start = token.find_first_not_of(" \t\r\n");
        if (start != std::string::npos) {
            auto end = token.find_last_not_of(" \t\r\n");
            tokens.push_back(token.substr(start, end - start + 1));
        }
    }
    return tokens;
}
} // namespace

int main(int argc, char* argv[])
{
    std::filesystem::path configPath = "proxy.conf";
    if (argc > 1) {
        configPath = argv[1];
    }

    Proxy::Log::info("Starting SplitHandler executable...");
    Proxy::Log::info("Loading configuration from: {}", configPath.string());

    Core::Configuration config = Core::Configuration::fromFile(configPath);

    std::unique_ptr<::Backend::IBackend> backend = ::Backend::createBackend();
    backend->configure(config);

    std::string inChannelName = config.getOr<std::string>("split.input_channel", "pipe_in");
    auto inputChannel = backend->createInputChannel(inChannelName, config);
    if (!inputChannel) {
        Proxy::Log::error("[SplitHandler] Failed to create input channel '{}'", inChannelName);
        return 1;
    }

    Proxy::Handler::SplitHandler handler;
    handler.registerInputChannel(inChannelName, std::move(inputChannel));

    std::string outChannelsStr = config.getOr<std::string>("split.output_channels", "");
    std::vector<std::string> outChannelNames = splitString(outChannelsStr, ',');

    for (const auto& outName : outChannelNames) {
        auto outChannel = backend->createOutputChannel(outName, config);
        if (!outChannel) {
            Proxy::Log::error("[SplitHandler] Failed to create output channel '{}'", outName);
            return 1;
        }
        handler.registerOutputChannel(outName, std::move(outChannel));
        Proxy::Log::info("[SplitHandler] Registered output channel '{}'", outName);
    }

    Proxy::Log::info("SplitHandler initialized (input='{}', output_channels={}). Starting event loop...",
        inChannelName, outChannelNames.size());

    handler.start();

    Proxy::Log::info("SplitHandler exiting cleanly.");
    return 0;
}
