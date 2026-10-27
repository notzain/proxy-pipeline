#pragma once

#include "backend/IOutputChannel.hpp"
#include "common/memory/ObserverPtr.hpp"
#include "core/message/MessageView.hpp"
#include "core/message/ProtocolId.hpp"
#include <cstdint>
#include <string>
#include <variant>

namespace Proxy::Handler {

namespace MatchCriteria {

    /**
     * @brief Matches MessageView::protocol() against an expected ProtocolId.
     */
    struct Protocol {
        Core::Message::ProtocolId expected;
    };

    /**
     * @brief Matches substring inside MessageView::payload().
     */
    struct PayloadContains {
        std::string pattern;
    };

    /**
     * @brief Matches prefix of MessageView::payload().
     */
    struct PayloadPrefix {
        std::string prefix;
    };

    /**
     * @brief Matches originating client UDP port in UdpMetadata.
     */
    struct ClientPort {
        std::uint16_t port;
    };

    /**
     * @brief Unconditional match (catch-all / default fallback route).
     */
    struct Fallback { };

} // namespace MatchCriteria

/**
 * @brief Type-safe sum type (tagged union) representing pattern matching criteria,
 * modeling Rust-style pattern matching via std::variant and std::visit.
 */
using RoutePattern = std::variant<
    MatchCriteria::Protocol,
    MatchCriteria::PayloadContains,
    MatchCriteria::PayloadPrefix,
    MatchCriteria::ClientPort,
    MatchCriteria::Fallback>;

/**
 * @brief Declarative routing rule binding a pattern variant to a destination output channel.
 */
class RouteRule {
public:
    RouteRule(RoutePattern pattern, std::string targetChannelName);

    [[nodiscard]] bool matches(const Core::Message::MessageView& message) const noexcept;
    [[nodiscard]] const std::string& targetChannelName() const noexcept;
    [[nodiscard]] Common::Memory::ObserverPtr<::Backend::IOutputChannel> targetChannel() const noexcept;
    void bindTargetChannel(Common::Memory::ObserverPtr<::Backend::IOutputChannel> channel) noexcept;

    [[nodiscard]] const RoutePattern& pattern() const noexcept;

private:
    RoutePattern m_pattern;
    std::string m_targetChannelName;
    Common::Memory::ObserverPtr<::Backend::IOutputChannel> m_targetChannel;
};

} // namespace Proxy::Handler
