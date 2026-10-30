#include "backend/vendor/VendorOutputChannel.hpp"
#include "VendorLeaseState.hpp"
#include "backend/MessageView.hpp"
#include "log/Log.hpp"
#include <utility>

namespace Backend::Vendor {

VendorOutputChannel::VendorOutputChannel(
    std::string name,
    std::shared_ptr<Sdk::VendorSender> sender)
    : ::Backend::IOutputChannel(std::move(name))
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

    if (auto state = message.consumeLeaseState()) {
        if (auto* vendorState = dynamic_cast<VendorLeaseState*>(state.get())) {
            Sdk::VendorMessage origMsg = vendorState->takeMessage();
            return m_sender->send(origMsg);
        }
    }

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
