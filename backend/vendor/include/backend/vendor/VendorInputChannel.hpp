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
 * Receives VendorMessage instances, packs the raw payload into the destination buffer
 * via MessageEnvelope, and attaches an out-of-band Backend::Lease owning the VendorMessage
 * to the BufferSlot so it is finalized or dropped upon handler completion.
 */
class VendorInputChannel : public ::Backend::IInputChannel {
public:
    VendorInputChannel(
        std::string name,
        std::shared_ptr<Sdk::VendorManager> manager,
        std::shared_ptr<Sdk::VendorReceiver> receiver = nullptr);

    VendorInputChannel(
        std::string name,
        VendorBackend& backend,
        std::shared_ptr<Sdk::VendorReceiver> receiver = nullptr);

    ~VendorInputChannel() override = default;

    ::Backend::BytesResult receiveSlot(
        ::Backend::BufferSlot& slot,
        Timeout timeout) override;

    void setReceiver(std::shared_ptr<Sdk::VendorReceiver> receiver);

private:
    std::shared_ptr<Sdk::VendorManager> m_manager;
    std::shared_ptr<Sdk::VendorReceiver> m_receiver;
};

} // namespace Backend::Vendor
