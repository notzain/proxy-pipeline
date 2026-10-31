#pragma once

#include "backend/channel/IInputChannel.hpp"
#include "backend/vendor/VendorSdkStub.hpp"
#include <cstddef>
#include <memory>
#include <string>

namespace Backend::Vendor {

class VendorBackend;
class VendorRelayPool;

/**
 * @brief Ingress channel wrapping the 3rd-party vendor Receiver.
 *
 * Each channel owns its private VendorRelayPool, ensuring zero contention across
 * channels and isolated capacity limits.
 *
 * Supports both vendor payload delivery modes with zero heap allocations on ingress:
 * 1. In-memory `{ byte*, length }` struct (Mode A): exposes a zero-copy leased raw view
 *    (`BufferSlot::assignLeased`) directly over the VendorMessage memory without copying.
 * 2. Payload file path (Mode B): directly memory-maps the file (`Common::Io::MappedFile`),
 *    completely eliminating userland buffer pool contention and 64 KB memory copies.
 */
class VendorInputChannel : public ::Backend::IInputChannel {
public:
    static constexpr std::size_t DefaultCapacity = 1024;

    VendorInputChannel(
        std::string name,
        std::shared_ptr<Sdk::VendorManager> manager,
        std::shared_ptr<Sdk::VendorReceiver> receiver = nullptr,
        std::size_t capacity = DefaultCapacity);

    VendorInputChannel(
        std::string name,
        VendorBackend& backend,
        std::shared_ptr<Sdk::VendorReceiver> receiver = nullptr,
        std::size_t capacity = DefaultCapacity);

    ~VendorInputChannel() override;

    [[nodiscard]] bool providesOwnMemory() const noexcept override;

    ::Backend::BytesResult receiveSlot(
        ::Backend::BufferSlot& slot,
        Timeout timeout) override;

    void setReceiver(std::shared_ptr<Sdk::VendorReceiver> receiver);

    [[nodiscard]] std::size_t capacity() const noexcept;

private:
    std::shared_ptr<Sdk::VendorManager> m_manager;
    std::shared_ptr<Sdk::VendorReceiver> m_receiver;
    std::unique_ptr<VendorRelayPool> m_relayPool;
};

} // namespace Backend::Vendor
