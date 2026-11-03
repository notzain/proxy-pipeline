#pragma once

#include "IRouteCondition.hpp"
#include <memory>
#include <string>

namespace Proxy::Handler {

/**
 * @brief Condition evaluated by an embedded Lua script predicate snippet or function.
 *
 * Exposes the message as a Lua table `msg` with fields:
 *   - `msg.payload`: string containing message bytes
 *   - `msg.protocol`: integer ProtocolId value
 *   - `msg.port`: client source UDP port (or nil if non-UDP)
 *
 * Returns true if the Lua snippet / script returns true, false otherwise.
 */
class LuaCondition : public IRouteCondition {
public:
    explicit LuaCondition(std::string scriptCode);
    ~LuaCondition() override;

    LuaCondition(const LuaCondition& other);
    LuaCondition& operator=(const LuaCondition& other);

    [[nodiscard]] bool matches(const Backend::MessageView& message) const noexcept override;
    [[nodiscard]] std::unique_ptr<IRouteCondition> clone() const override;

    [[nodiscard]] const std::string& scriptCode() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Proxy::Handler

