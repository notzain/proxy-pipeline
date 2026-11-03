#pragma once

#include "IRouteCondition.hpp"
#include <cstdint>

namespace Proxy::Handler {

/**
 * @brief Condition inspecting originating UDP client port from message metadata.
 */
class PortCondition : public IRouteCondition {
public:
    explicit PortCondition(std::uint16_t port);

    [[nodiscard]] bool matches(const Backend::MessageView& message) const noexcept override;
    [[nodiscard]] std::unique_ptr<IRouteCondition> clone() const override;

    [[nodiscard]] std::uint16_t port() const noexcept;

private:
    std::uint16_t m_port;
};

} // namespace Proxy::Handler
