#include "PrintHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/message/MessageView.hpp"
#include "core/config/Configuration.hpp"
#include "log/Log.hpp"
#include <cctype>
#include <fmt/core.h>
#include <string>

namespace Proxy::Handler {

bool PrintHandler::onConfigure(const Core::Config::Configuration& config)
{
    const auto channelName = config.getOr<std::string>("print.channel", "default");
    return tryBindInputChannel(channelName, config).has_value();
}

Backend::MessageResult PrintHandler::onMessageReceived(std::string_view channelName, const Backend::MessageView& message)
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

    PROXY_LOG_INFO("Channel '{}' received message ({} bytes): {}", channelName, payload.size(), textFormatted);
    PROXY_LOG_DEBUG("Hex dump: {}", hexFormatted);
    return Backend::Consumed { };
}

void PrintHandler::onBatchReceived(
    std::string_view channelName,
    Common::Compat::span<const Backend::MessageView> messages,
    Common::Compat::span<Backend::MessageResult> outResults)
{
    PROXY_LOG_INFO("Channel '{}' received batch of {} message(s)", channelName, messages.size());
    const auto count = std::min(messages.size(), outResults.size());
    for (std::size_t i = 0; i < count; ++i) {
        outResults[i] = onMessageReceived(channelName, messages[i]);
    }
}

} // namespace Proxy::Handler
