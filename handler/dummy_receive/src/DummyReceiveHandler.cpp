#include "DummyReceiveHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "core/message/MessageView.hpp"
#include "log/Log.hpp"
#include <cctype>
#include <fmt/core.h>

namespace Proxy::Handler {

bool DummyReceiveHandler::onConfigure(const Core::Config::Configuration& config)
{
    const auto channelName = config.getOr<std::string>("dummy.channel", "test_channel");
    auto inChannel = backend().createInputChannel(channelName, config);
    if (!inChannel) {
        Proxy::Log::error("[DummyReceive] Failed to create input channel '{}'", channelName);
        return false;
    }
    registerInputChannel(std::move(inChannel));
    return true;
}

Core::Message::MessageResult DummyReceiveHandler::onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message)
{
    auto payload = message.payload();
    std::string textFormatted;
    std::string hexFormatted;

    for (const auto b : payload) {
        const auto val = static_cast<unsigned char>(b);
        if (std::isprint(val)) {
            textFormatted += static_cast<char>(val);
        } else {
            textFormatted += fmt::format("\\x{:02x}", val);
        }
        hexFormatted += fmt::format("{:02x} ", val);
    }

    Proxy::Log::info("[DummyReceive] Channel '{}' received message ({} bytes): {}", channelName, payload.size(), textFormatted);
    Proxy::Log::debug("[DummyReceive] Channel '{}' hex dump: {}", channelName, hexFormatted);
    return Core::Message::Consumed { };
}

} // namespace Proxy::Handler
