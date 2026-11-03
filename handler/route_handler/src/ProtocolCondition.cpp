#include "ProtocolCondition.hpp"

namespace Proxy::Handler {

ProtocolCondition::ProtocolCondition(Backend::ProtocolId protocol)
    : m_protocol(protocol)
{
}

bool ProtocolCondition::matches(const Backend::MessageView& message) const noexcept
{
    return message.protocol() == m_protocol;
}

std::unique_ptr<IRouteCondition> ProtocolCondition::clone() const
{
    return std::make_unique<ProtocolCondition>(*this);
}

Backend::ProtocolId ProtocolCondition::protocol() const noexcept
{
    return m_protocol;
}

} // namespace Proxy::Handler
