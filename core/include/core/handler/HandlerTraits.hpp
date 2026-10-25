#pragma once

#include <string_view>

namespace Core::Handler {

/**
 * @brief High-level operational role of a handler in the pipeline topology.
 */
enum class HandlerRole {
    Processor,       ///< Standard intermediate processing node (receives via input channel, sends to output channel)
    Generator,       ///< Autonomous synthetic message generator driven by internal timers or step() ticks
    ExternalIngress, ///< Gateway receiving messages from external network protocols (UDP, HTTP, etc.)
    ExternalEgress   ///< Gateway sending pipeline messages outwards to external network protocols
};

/**
 * @brief External network/transport protocol utilized by ingress or egress handlers.
 */
enum class HandlerProtocol {
    None, ///< No external protocol (operates exclusively on internal pipeline channels)
    Udp,  ///< UDP network datagrams
    Http  ///< HTTP / REST protocol
};

/**
 * @brief Converts a HandlerRole to its canonical string representation.
 */
[[nodiscard]] constexpr std::string_view toString(HandlerRole role) noexcept
{
    switch (role) {
    case HandlerRole::Processor:
        return "processor";
    case HandlerRole::Generator:
        return "generator";
    case HandlerRole::ExternalIngress:
        return "external_ingress";
    case HandlerRole::ExternalEgress:
        return "external_egress";
    }
    return "processor";
}

/**
 * @brief Converts a HandlerProtocol to its canonical string representation.
 */
[[nodiscard]] constexpr std::string_view toString(HandlerProtocol protocol) noexcept
{
    switch (protocol) {
    case HandlerProtocol::None:
        return "none";
    case HandlerProtocol::Udp:
        return "udp";
    case HandlerProtocol::Http:
        return "http";
    }
    return "none";
}

} // namespace Core::Handler

