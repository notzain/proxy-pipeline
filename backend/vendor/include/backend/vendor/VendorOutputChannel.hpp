#pragma once

#include "backend/channel/IOutputChannel.hpp"
#include "backend/vendor/VendorSdkStub.hpp"
#include <memory>
#include <string>

namespace Backend::Vendor {

/**
 * @brief Egress channel wrapping the 3rd-party vendor Sender.
 *
 * Messages received from a VendorInputChannel of the same VendorManager are forwarded as the exact
 * original VendorMessage (consuming forward). All other messages are re-materialized from their payload.
 */
class VendorOutputChannel : public ::Backend::IOutputChannel {
public:
    explicit VendorOutputChannel(
        std::string name,
        std::shared_ptr<Sdk::VendorManager> manager,
        std::shared_ptr<Sdk::VendorSender> sender = nullptr);

    ~VendorOutputChannel() override = default;

    using ::Backend::IOutputChannel::send;
    using ::Backend::IOutputChannel::sendBatch;

    bool send(Common::Compat::span<const std::byte> message) override;
    bool send(const ::Backend::MessageView& message) override;
    std::size_t sendBatch(Common::Compat::span<const ::Backend::MessageView> messages) override;

    void setSender(std::shared_ptr<Sdk::VendorSender> sender);

private:
    std::shared_ptr<Sdk::VendorManager> m_manager;
    std::shared_ptr<Sdk::VendorSender> m_sender;
};

} // namespace Backend::Vendor
