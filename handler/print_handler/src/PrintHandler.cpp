#include "PrintHandler.hpp"
#include "log/Log.hpp"
#include <cctype>
#include <fmt/core.h>
#include <string>

namespace Proxy::Handler {

void PrintHandler::onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer)
{
    std::string textFormatted;
    std::string hexFormatted;

    for (const auto b : buffer) {
        const auto val = static_cast<unsigned char>(b);
        if (std::isprint(val)) {
            textFormatted += static_cast<char>(val);
        } else {
            textFormatted += fmt::format("\\x{:02x}", val);
        }
        hexFormatted += fmt::format("{:02x} ", val);
    }

    Proxy::Log::info("[PrintHandler] Channel '{}' received message ({} bytes): {}", channelName, buffer.size(), textFormatted);
    Proxy::Log::debug("[PrintHandler] Hex dump: {}", hexFormatted);
}

} // namespace Proxy::Handler
