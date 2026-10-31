#include "VendorLeaseState.hpp"
#include "log/Log.hpp"
#include <utility>

namespace Backend::Vendor {

VendorLeaseState::VendorLeaseState(
    Sdk::VendorMessage message,
    std::shared_ptr<Sdk::VendorManager> manager,
    Common::Memory::DynamicBuffer buffer) noexcept
    : m_message(std::move(message))
    , m_manager(std::move(manager))
    , m_buffer(std::move(buffer))
{
}

void VendorLeaseState::settle(::Backend::Settlement outcome) noexcept
{
    if (!m_manager || m_consumed) {
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

Common::Compat::span<const std::byte> VendorLeaseState::payloadView() const noexcept
{
    if (Common::Memory::hasData(m_buffer)) {
        return Common::Memory::spanOf(m_buffer);
    }
    const auto bytePayload = m_message.getBytePayload();
    if (bytePayload.data != nullptr) {
        return Common::Compat::span<const std::byte>(bytePayload.data, bytePayload.length);
    }
    return { };
}

const Sdk::VendorMessage& VendorLeaseState::message() const noexcept
{
    return m_message;
}

bool VendorLeaseState::belongsTo(const Sdk::VendorManager& manager) const noexcept
{
    return m_manager.get() == &manager;
}

void VendorLeaseState::markConsumed() noexcept
{
    m_consumed = true;
}

bool VendorLeaseState::isConsumed() const noexcept
{
    return m_consumed;
}

} // namespace Backend::Vendor
