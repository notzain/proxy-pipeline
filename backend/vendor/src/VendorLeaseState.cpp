#include "VendorLeaseState.hpp"
#include "log/Log.hpp"
#include <utility>

namespace Backend::Vendor {

VendorLeaseState::VendorLeaseState(
    Sdk::VendorMessage message,
    std::shared_ptr<Sdk::VendorManager> manager) noexcept
    : m_message(std::move(message))
    , m_manager(std::move(manager))
{
}

void VendorLeaseState::settle(::Backend::Settlement outcome) noexcept
{
    if (!m_manager) {
        return;
    }

    const std::uint64_t msgId = m_message.getId();
    switch (outcome) {
    case ::Backend::Settlement::Ack:
        Proxy::Log::debug("[VendorLeaseState] Finalizing message ID {}", msgId);
        m_manager->finalize(m_message);
        break;
    case ::Backend::Settlement::Reject:
    case ::Backend::Settlement::Release:
        Proxy::Log::debug("[VendorLeaseState] Dropping message ID {}", msgId);
        m_manager->drop(m_message);
        break;
    }
}

} // namespace Backend::Vendor
