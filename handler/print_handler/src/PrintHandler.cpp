#include "PrintHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "core/message/MessageView.hpp"
#include "log/Log.hpp"
#include <cctype>
#include <fmt/core.h>
#include <string>

namespace Proxy::Handler {

bool PrintHandler::configure(const Core::Config::Configuration& config, ::Backend::IBackend& backend)
{
    IHandler::configure(config, backend);

    m_channelName = config.getOr<std::string>("print.channel", "default");
    auto inChannel = backend.createInputChannel(m_channelName, config);
    if (!inChannel) {
        Proxy::Log::error("[PrintHandler] Failed to create input channel '{}'", m_channelName);
        return false;
    }
    m_inputChannel = registerInputChannel(m_channelName, std::move(inChannel));
    return true;
}

Core::Message::MessageResult PrintHandler::onMessageReceived(std::string_view channelName, const Core::Message::MessageView& message)
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

    Proxy::Log::info("[PrintHandler] Channel '{}' received message ({} bytes): {}", channelName, payload.size(), textFormatted);
    Proxy::Log::debug("[PrintHandler] Hex dump: {}", hexFormatted);
    return Core::Message::Consumed { };
}

void PrintHandler::onBatchReceived(std::string_view channelName, std::span<const Core::Message::MessageView> messages)
{
    Proxy::Log::info("[PrintHandler] Channel '{}' received batch of {} message(s)", channelName, messages.size());
    for (const auto& msg : messages) {
        onMessageReceived(channelName, msg);
    }
}

} // namespace Proxy::Handler
