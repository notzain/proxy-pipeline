#include "backend/vendor/VendorInputChannel.hpp"
#include "backend/MessageEnvelope.hpp"
#include "backend/vendor/VendorBackend.hpp"
#include "backend/vendor/VendorMetadata.hpp"
#include "common/memory/ByteView.hpp"
#include "log/Log.hpp"

namespace Backend::Vendor {

VendorInputChannel::VendorInputChannel(
    std::string name,
    VendorBackend& backend,
    std::shared_ptr<Sdk::VendorReceiver> receiver)
    : ::Backend::IInputChannel(std::move(name))
    , m_backend(backend)
    , m_receiver(std::move(receiver))
{
}

void VendorInputChannel::setReceiver(std::shared_ptr<Sdk::VendorReceiver> receiver)
{
    m_receiver = std::move(receiver);
}

::Backend::BytesResult VendorInputChannel::receiveInto(
    Common::Compat::span<std::byte> destination,
    Timeout timeout)
{
    if (!m_receiver) {
        return ::Backend::BytesResult::failure(::Backend::ReceiveClosed {});
    }

    Sdk::VendorMessage rawMsg;
    if (!m_receiver->receive(&rawMsg, static_cast<int>(timeout.count()))) {
        return ::Backend::BytesResult::failure(::Backend::ReceiveTimeout {});
    }

    const std::uint64_t msgId = rawMsg.getId();
    auto mgr = m_backend.vendorManager();
    if (!mgr) {
        Proxy::Log::error("[VendorInputChannel] VendorManager not initialized");
        return ::Backend::BytesResult::failure(::Backend::ReceiveFailure {});
    }

    // 1. Wrap in RAII ScopedVendorMessage and register with the backend
    m_backend.registerInFlight(msgId, ScopedVendorMessage(std::move(rawMsg), *mgr));

    // 2. Fetch payload
    auto scopedOpt = m_backend.unregisterInFlight(msgId);
    if (!scopedOpt) {
        return ::Backend::BytesResult::failure(::Backend::ReceiveFailure {});
    }
    std::string payload = scopedOpt->getPayload();
    m_backend.registerInFlight(msgId, std::move(*scopedOpt));

    VendorMetadata meta { msgId };
    std::size_t written = ::Backend::MessageEnvelope::packInto(
        destination,
        ::Backend::ProtocolId::Raw,
        Common::Memory::ByteView(meta),
        Common::Memory::ByteView(payload));

    if (written == 0) {
        Proxy::Log::error("[VendorInputChannel] Destination buffer too small for message ID {}", msgId);
        auto droppedMsg = m_backend.unregisterInFlight(msgId);
        if (droppedMsg) {
            droppedMsg->drop();
        }
        return ::Backend::BytesResult::failure(::Backend::ReceiveFailure {});
    }

    return ::Backend::BytesResult::success(written);
}

} // namespace Backend::Vendor
