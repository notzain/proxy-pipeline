#pragma once

#include "backend/IInputChannel.hpp"
#include "backend/vendor/VendorSdkStub.hpp"
#include <memory>
#include <string>

namespace Backend::Vendor {

class VendorBackend;

/**
 * @brief Ingress channel wrapping the 3rd-party vendor Receiver.
 *
 * Receives VendorMessage instances, registers them into VendorBackend, and packs
 * [VendorMetadata(msgId) | Payload] into the destination buffer via MessageEnvelope.
 */
class VendorInputChannel : public ::Backend::IInputChannel {
public:
    VendorInputChannel(
        std::string name,
        VendorBackend& backend,
        std::shared_ptr<Sdk::VendorReceiver> receiver = nullptr);

    ~VendorInputChannel() override = default;

    ::Backend::BytesResult receiveInto(
        Common::Compat::span<std::byte> destination,
        Timeout timeout) override;

    void setReceiver(std::shared_ptr<Sdk::VendorReceiver> receiver);

private:
    VendorBackend& m_backend;
    std::shared_ptr<Sdk::VendorReceiver> m_receiver;
};

} // namespace Backend::Vendor

