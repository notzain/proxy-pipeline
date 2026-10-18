#include "DummyReceiveHandler.hpp"
#include "log/Log.hpp"
#include <cctype>
#include <fmt/core.h>

namespace Proxy::Handler {

void DummyReceiveHandler::onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer)
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

    Proxy::Log::info("[DummyReceive] Channel '{}' received message ({} bytes): {}", channelName, buffer.size(), textFormatted);
    Proxy::Log::debug("[DummyReceive] Channel '{}' hex dump: {}", channelName, hexFormatted);
}

} // namespace Proxy::Handler
