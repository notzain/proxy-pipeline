#pragma once

#include "backend/IOutputChannel.hpp"
#include "backend/vendor/VendorSdkStub.hpp"
#include <memory>
#include <string>

namespace Backend::Vendor {

/**
 * @brief Egress channel wrapping the 3rd-party vendor Sender.
 */
class VendorOutputChannel : public ::Backend::IOutputChannel {
public:
    explicit VendorOutputChannel(
        std::string name,
        std::shared_ptr<Sdk::VendorSender> sender = nullptr);

    ~VendorOutputChannel() override = default;

    bool send(Common::Compat::span<const std::byte> message) override;

    void setSender(std::shared_ptr<Sdk::VendorSender> sender);

private:
    std::shared_ptr<Sdk::VendorSender> m_sender;
};

} // namespace Backend::Vendor
