#pragma once

#include "backend/IBackend.hpp"
#include "backend/vendor/VendorSdkStub.hpp"
#include <memory>

namespace Backend::Vendor {

/**
 * @brief 3rd-party vendor transport backend implementation.
 *
 * Manages the shared vendor SDK manager and creates vendor input/output channels.
 * In-flight vendor messages are owned directly by out-of-band Backend::Lease instances
 * produced by VendorInputChannel rather than a backend-wide registry.
 */
class VendorBackend : public ::Backend::IBackend {
public:
    VendorBackend();
    explicit VendorBackend(std::shared_ptr<Sdk::VendorManager> manager);
    ~VendorBackend() override;

    void configure(const ::Backend::IConfig& config) override;

    std::unique_ptr<::Backend::IInputChannel> createInputChannel(
        std::string_view name,
        const ::Backend::IConfig& config) override;

    std::unique_ptr<::Backend::IOutputChannel> createOutputChannel(
        std::string_view name,
        const ::Backend::IConfig& config) override;

    [[nodiscard]] std::shared_ptr<Sdk::VendorManager> vendorManager() const noexcept;

private:
    std::shared_ptr<Sdk::VendorManager> m_vendorManager;
};

} // namespace Backend::Vendor
