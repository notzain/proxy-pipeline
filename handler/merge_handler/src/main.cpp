#include "MergeHandler.hpp"
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

    Proxy::Log::info("Starting MergeHandler executable...");
    Proxy::Log::info("Loading configuration from: {}", configPath.string());

    Core::Configuration config = Core::Configuration::fromFile(configPath);

    std::unique_ptr<::Backend::IBackend> backend = ::Backend::createBackend();
    backend->configure(config);

    std::string outChannelName = config.getOr<std::string>("merge.output_channel", "pipe_out");
    auto outputChannel = backend->createOutputChannel(outChannelName, config);
    if (!outputChannel) {
        Proxy::Log::error("[MergeHandler] Failed to create output channel '{}'", outChannelName);
        return 1;
    }

    Proxy::Handler::MergeHandler handler(outChannelName);
    handler.registerOutputChannel(outChannelName, std::move(outputChannel));

    std::string inChannelsStr = config.getOr<std::string>("merge.input_channels", "");
    std::vector<std::string> inChannelNames = splitString(inChannelsStr, ',');

    for (const auto& inName : inChannelNames) {
        auto inChannel = backend->createInputChannel(inName, config);
        if (!inChannel) {
            Proxy::Log::error("[MergeHandler] Failed to create input channel '{}'", inName);
            return 1;
        }
        handler.registerInputChannel(inName, std::move(inChannel));
        Proxy::Log::info("[MergeHandler] Registered input channel '{}'", inName);
    }

    Proxy::Log::info("MergeHandler initialized (input_channels={}, output='{}'). Starting event loop...",
        inChannelNames.size(), outChannelName);

    handler.start();

    Proxy::Log::info("MergeHandler exiting cleanly.");
    return 0;
}

