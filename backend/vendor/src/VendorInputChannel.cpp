#include "backend/vendor/VendorInputChannel.hpp"
#include "VendorLeaseState.hpp"
#include "backend/MessageEnvelope.hpp"
#include "backend/vendor/VendorBackend.hpp"
#include "common/memory/ByteView.hpp"
#include "log/Log.hpp"
#include <utility>

namespace Backend::Vendor {

VendorInputChannel::VendorInputChannel(
    std::string name,
    std::shared_ptr<Sdk::VendorManager> manager,
    std::shared_ptr<Sdk::VendorReceiver> receiver)
    : ::Backend::IInputChannel(std::move(name))
    , m_manager(std::move(manager))
    , m_receiver(std::move(receiver))
{
}

VendorInputChannel::VendorInputChannel(
    std::string name,
    VendorBackend& backend,
    std::shared_ptr<Sdk::VendorReceiver> receiver)
    : VendorInputChannel(std::move(name), backend.vendorManager(), std::move(receiver))
{
}

void VendorInputChannel::setReceiver(std::shared_ptr<Sdk::VendorReceiver> receiver)
{
    m_receiver = std::move(receiver);
}

::Backend::BytesResult VendorInputChannel::receiveSlot(
    ::Backend::BufferSlot& slot,
    Timeout timeout)
{
    slot.size = 0;
    slot.lease.release();

    if (!m_receiver) {
        return ::Backend::BytesResult::failure(::Backend::ReceiveClosed { });
    }

    if (!m_manager) {
        Proxy::Log::error("[VendorInputChannel] VendorManager not initialized");
        return ::Backend::BytesResult::failure(::Backend::ReceiveFailure { });
    }

    Sdk::VendorMessage rawMsg;
    if (!m_receiver->receive(&rawMsg, static_cast<int>(timeout.count()))) {
        return ::Backend::BytesResult::failure(::Backend::ReceiveTimeout { });
    }

    const std::string payload = rawMsg.getPayload();
    const std::size_t written = ::Backend::MessageEnvelope::packInto(
        slot.buffer,
        ::Backend::ProtocolId::Raw,
        Common::Compat::span<const std::byte> { },
        Common::Memory::ByteView(payload));

    if (written == 0) {
        Proxy::Log::error("[VendorInputChannel] Destination buffer too small for message ID {}", rawMsg.getId());
        m_manager->drop(rawMsg);
        return ::Backend::BytesResult::failure(::Backend::ReceiveFailure { });
    }

    slot.size = written;
    slot.lease = ::Backend::Lease(std::make_unique<VendorLeaseState>(std::move(rawMsg), m_manager));
    return ::Backend::BytesResult::success(written);
}

} // namespace Backend::Vendor
