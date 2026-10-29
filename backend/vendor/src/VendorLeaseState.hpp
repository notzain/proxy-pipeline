#pragma once

#include "backend/ILeaseState.hpp"
#include "backend/vendor/VendorSdkStub.hpp"
#include <memory>

namespace Backend::Vendor {

/**
 * @brief Transport-specific lease state owning a single in-flight VendorMessage.
 *
 * Holds the vendor message and a shared reference to the VendorManager, settling
 * the message directly via finalize() or drop() without any backend registry.
 */
class VendorLeaseState final : public ::Backend::ILeaseState {
public:
    VendorLeaseState(
        Sdk::VendorMessage message,
        std::shared_ptr<Sdk::VendorManager> manager) noexcept;

    ~VendorLeaseState() override = default;

    void settle(::Backend::Settlement outcome) noexcept override;

private:
    Sdk::VendorMessage m_message;
    std::shared_ptr<Sdk::VendorManager> m_manager;
};

} // namespace Backend::Vendor
