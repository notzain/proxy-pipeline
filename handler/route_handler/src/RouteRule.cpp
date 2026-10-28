#include "RouteRule.hpp"
#include "backend/UdpMetadata.hpp"
#include "common/compat/StringView.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <string_view>

namespace Proxy::Handler {

namespace {

    /**
     * @brief Matcher visitor providing pattern matching semantics analogous to Rust's `match`.
     */
    struct PatternMatcher {
        const Backend::MessageView& message;

        bool operator()(const MatchCriteria::Protocol& c) const noexcept
        {
            return message.protocol() == c.expected;
        }

        bool operator()(const MatchCriteria::PayloadContains& c) const noexcept
        {
            if (c.pattern.empty()) {
                return true;
            }
            auto payload = message.payload();
            if (payload.size() < c.pattern.size()) {
                return false;
            }
            std::string_view payloadStr(reinterpret_cast<const char*>(payload.data()), payload.size());
            return payloadStr.find(c.pattern) != std::string_view::npos;
        }

        bool operator()(const MatchCriteria::PayloadPrefix& c) const noexcept
        {
            if (c.prefix.empty()) {
                return true;
            }
            auto payload = message.payload();
            if (payload.size() < c.prefix.size()) {
                return false;
            }
            std::string_view payloadStr(reinterpret_cast<const char*>(payload.data()), payload.size());
            return Common::Compat::starts_with(payloadStr, c.prefix);
        }

        bool operator()(const MatchCriteria::ClientPort& c) const noexcept
        {
            const auto* udpMeta = message.getMetadata<Backend::UdpMetadata>();
            if (!udpMeta) {
                return false;
            }
            std::uint16_t clientPort = ntohs(udpMeta->srcPort);
            return clientPort == c.port;
        }

        bool operator()(const MatchCriteria::Fallback&) const noexcept
        {
            return true;
        }
    };

} // namespace

RouteRule::RouteRule(RoutePattern pattern, std::string targetChannelName)
    : m_pattern(std::move(pattern))
    , m_targetChannelName(std::move(targetChannelName))
{
}

bool RouteRule::matches(const Backend::MessageView& message) const noexcept
{
    return m_pattern.match(PatternMatcher { message });
}

const std::string& RouteRule::targetChannelName() const noexcept
{
    return m_targetChannelName;
}

Common::Memory::ObserverPtr<::Backend::IOutputChannel> RouteRule::targetChannel() const noexcept
{
    return m_targetChannel;
}

void RouteRule::bindTargetChannel(Common::Memory::ObserverPtr<::Backend::IOutputChannel> channel) noexcept
{
    m_targetChannel = channel;
}

const RoutePattern& RouteRule::pattern() const noexcept
{
    return m_pattern;
}

} // namespace Proxy::Handler
