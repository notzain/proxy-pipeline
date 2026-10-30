#pragma once

#include "backend/IInputChannel.hpp"
#include "backend/vendor/VendorSdkStub.hpp"
#include "common/memory/BufferPool.hpp"
#include <memory>
#include <string>

namespace Backend::Vendor {

class VendorBackend;

/**
 * @brief Ingress channel wrapping the 3rd-party vendor Receiver.
 *
 * Supports both vendor payload delivery modes:
 * 1. In-memory `{ byte*, length }` struct (Mode A): exposes a zero-copy leased raw view
 *    (`BufferSlot::assignLeased`) directly over the VendorMessage memory without copying into `slot.buffer`.
 * 2. Payload file path (Mode B): reads the file into a `BufferSlice` acquired from `VendorBackend`'s
 *    `BufferPool` (or a private memory mapping if the file exceeds the block size or the pool is exhausted)
 *    owned by the `VendorLeaseState`.
 */
class VendorInputChannel : public ::Backend::IInputChannel {
public:
    VendorInputChannel(
        std::string name,
        std::shared_ptr<Sdk::VendorManager> manager,
        std::shared_ptr<Sdk::VendorReceiver> receiver = nullptr,
        std::shared_ptr<Common::Memory::BufferPool> bufferPool = nullptr);

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
    std::shared_ptr<Common::Memory::BufferPool> m_bufferPool;
};

} // namespace Backend::Vendor
