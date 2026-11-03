#pragma once

#include "IRouteCondition.hpp"
#include "backend/message/ProtocolId.hpp"

namespace Proxy::Handler {

/**
 * @brief Condition inspecting message protocol ID.
 */
class ProtocolCondition : public IRouteCondition {
public:
    explicit ProtocolCondition(Backend::ProtocolId protocol);

    [[nodiscard]] bool matches(const Backend::MessageView& message) const noexcept override;
    [[nodiscard]] std::unique_ptr<IRouteCondition> clone() const override;

    [[nodiscard]] Backend::ProtocolId protocol() const noexcept;

private:
    Backend::ProtocolId m_protocol;
};

} // namespace Proxy::Handler
