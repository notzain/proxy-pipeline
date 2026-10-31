#include "backend/vendor/VendorOutputChannel.hpp"
#include "VendorRelayNode.hpp"
#include "backend/message/MessageView.hpp"
#include "log/Log.hpp"
#include <utility>

namespace Backend::Vendor {

VendorOutputChannel::VendorOutputChannel(
    std::string name,
    std::shared_ptr<Sdk::VendorManager> manager,
    std::shared_ptr<Sdk::VendorSender> sender)
    : ::Backend::IOutputChannel(std::move(name))
    , m_manager(std::move(manager))
    , m_sender(std::move(sender))
{
}

void VendorOutputChannel::setSender(std::shared_ptr<Sdk::VendorSender> sender)
{
    m_sender = std::move(sender);
}

bool VendorOutputChannel::send(Common::Compat::span<const std::byte> message)
{
    if (!m_sender) {
        Proxy::Log::warn("[VendorOutputChannel] send() called without initialized VendorSender on channel '{}'", name());
        return false;
    }

    auto payloadSpan = message;
    if (const auto view = ::Backend::MessageView::from(message)) {
        payloadSpan = view->payload();
    }

    std::string payloadStr(
        reinterpret_cast<const char*>(payloadSpan.data()),
        payloadSpan.size());

    Sdk::VendorMessage outMsg(0, std::move(payloadStr));
    return m_sender->send(outMsg);
}

bool VendorOutputChannel::send(const ::Backend::MessageView& message)
{
    if (!m_sender) {
        Proxy::Log::warn("[VendorOutputChannel] send() called without initialized VendorSender on channel '{}'", name());
        return false;
    }

    // Inspect the underlying token to verify origin, ownership, and manager compatibility
    if (auto token = message.token(); token && token->settler() == &VendorRelayNode::settle) {
        auto* node = static_cast<VendorRelayNode*>(token->context());
        if (node && !node->consumed && node->manager == m_manager) {
            const auto nodeSpan = node->payloadView();
            const auto msgSpan = message.payload();
            const bool payloadUnmodified = (nodeSpan.data() == msgSpan.data() && nodeSpan.size() == msgSpan.size());
            if (payloadUnmodified) {
                // Consuming forward for the same VendorManager:
                // Send the original message by reference first; only mark consumed if send succeeds!
                if (!m_sender->send(node->message)) {
                    return false;
                }
                node->consumed = true;
                token->disarm();
                return true;
            }
        }
    }

    // Fallback if payload was transformed via withPayload(), cross-manager, non-token, or already consumed by fan-out
    const auto payloadSpan = message.payload();
    std::string payloadStr(
        reinterpret_cast<const char*>(payloadSpan.data()),
        payloadSpan.size());

    Sdk::VendorMessage outMsg(0, std::move(payloadStr));
    return m_sender->send(outMsg);
}

std::size_t VendorOutputChannel::sendBatch(Common::Compat::span<const ::Backend::MessageView> messages)
{
    std::size_t count = 0;
    for (const auto& msg : messages) {
        if (send(msg)) {
            ++count;
        }
    }
    return count;
}

} // namespace Backend::Vendor
