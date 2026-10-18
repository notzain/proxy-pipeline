#include "SplitHandler.hpp"
#include "log/Log.hpp"

namespace Proxy::Handler {

void SplitHandler::onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer)
{
    Proxy::Log::info("[SplitHandler] Channel '{}' received message ({} bytes), splitting to output channels...", channelName, buffer.size());
    std::size_t forwarded = sendAll(buffer);
    Proxy::Log::debug("[SplitHandler] Successfully forwarded message to {} output channels", forwarded);
}

} // namespace Proxy::Handler
