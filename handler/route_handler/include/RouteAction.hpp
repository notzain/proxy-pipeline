#pragma once

#include "backend/channel/IOutputChannel.hpp"
#include "backend/message/MessageResult.hpp"
#include "common/memory/ObserverPtr.hpp"
#include "common/types/Variant.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace Proxy::Handler {

/**
 * @brief Forward action details.
 */
struct ForwardAction {
    std::string targetChannelName;
    Common::Memory::ObserverPtr<::Backend::IOutputChannel> targetChannel;
};

/**
 * @brief Reject action details with explicit reason and error code.
 */
struct RejectAction {
    std::string reason;
    int errorCode { 400 };
};

/**
 * @brief Sum type representing the routing action taken upon condition match.
 */
class RouteAction {
public:
    static RouteAction forward(std::string targetChannelName)
    {
        return RouteAction(ForwardAction { std::move(targetChannelName), nullptr });
    }

    static RouteAction reject(std::string reason, int errorCode = 400)
    {
        return RouteAction(RejectAction { std::move(reason), errorCode });
    }

    [[nodiscard]] bool isForward() const noexcept
    {
        return m_action.is<ForwardAction>();
    }

    [[nodiscard]] bool isReject() const noexcept
    {
        return m_action.is<RejectAction>();
    }

    [[nodiscard]] const ForwardAction* asForward() const noexcept
    {
        auto opt = m_action.get<ForwardAction>();
        return opt ? &opt->get() : nullptr;
    }

    [[nodiscard]] ForwardAction* asForward() noexcept
    {
        auto opt = m_action.get<ForwardAction>();
        return opt ? &opt->get() : nullptr;
    }

    [[nodiscard]] const RejectAction* asReject() const noexcept
    {
        auto opt = m_action.get<RejectAction>();
        return opt ? &opt->get() : nullptr;
    }

    void bindTargetChannel(Common::Memory::ObserverPtr<::Backend::IOutputChannel> channel) noexcept
    {
        if (auto* fwd = asForward()) {
            fwd->targetChannel = channel;
        }
    }

private:
    explicit RouteAction(ForwardAction action)
        : m_action(std::move(action))
    {
    }

    explicit RouteAction(RejectAction action)
        : m_action(std::move(action))
    {
    }

    Common::Types::Variant<ForwardAction, RejectAction> m_action;
};

} // namespace Proxy::Handler
