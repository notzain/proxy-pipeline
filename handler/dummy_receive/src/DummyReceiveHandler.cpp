#include "DummyReceiveHandler.hpp"
#include "backend/IBackend.hpp"
#include "core/config/Configuration.hpp"
#include "log/Log.hpp"
#include <cctype>
#include <fmt/core.h>

namespace Proxy::Handler {

bool DummyReceiveHandler::configure(const Core::Config::Configuration& config, ::Backend::IBackend& backend)
{
    m_channelName = config.getOr<std::string>("dummy.channel", "test_channel");
    auto inChannel = backend.createInputChannel(m_channelName, config);
    if (!inChannel) {
        Proxy::Log::error("[DummyReceive] Failed to create input channel '{}'", m_channelName);
        return false;
    }
    m_inputChannel = registerInputChannel(m_channelName, std::move(inChannel));
    return true;
}

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
