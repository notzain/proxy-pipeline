#include "PrintHandler.hpp"
#include <cctype>
#include <fmt/core.h>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

namespace Proxy::Handler {

PrintHandler::PrintHandler(std::unique_ptr<Core::IBackend> backend)
    : m_backend(std::move(backend))
{
}

void PrintHandler::onMessageReceived(std::span<const std::byte> buffer)
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

    spdlog::info("[PrintHandler] Received message ({} bytes): {}", buffer.size(), textFormatted);
    spdlog::debug("[PrintHandler] Hex dump: {}", hexFormatted);
}

std::optional<Core::IHandler::Buffer> PrintHandler::nextMessage(Timeout timeout)
{
    if (m_backend) {
        return m_backend->nextMessage(timeout);
    }
    return std::nullopt;
}

} // namespace Proxy::Handler
