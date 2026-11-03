#include "PortCondition.hpp"
#include "core/net/UdpMetadata.hpp"
#include <arpa/inet.h>

namespace Proxy::Handler {

PortCondition::PortCondition(std::uint16_t port)
    : m_port(port)
{
}

bool PortCondition::matches(const Backend::MessageView& message) const noexcept
{
    const auto udpMeta = message.getMetadata<Core::Net::UdpMetadata>();
    if (!udpMeta) {
        return false;
    }
    std::uint16_t clientPort = ntohs(udpMeta->srcPort);
    return clientPort == m_port;
}

std::unique_ptr<IRouteCondition> PortCondition::clone() const
{
    return std::make_unique<PortCondition>(*this);
}

std::uint16_t PortCondition::port() const noexcept
{
    return m_port;
}

} // namespace Proxy::Handler
