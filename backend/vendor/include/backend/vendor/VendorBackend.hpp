#pragma once

#include "backend/IBackend.hpp"
#include "backend/vendor/ScopedVendorMessage.hpp"
#include "backend/vendor/VendorSdkStub.hpp"
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace Backend::Vendor {

/**
 * @brief 3rd-party vendor transport backend implementation.
 *
 * Manages the vendor SDK manager lifecycle, registers in-flight ScopedVendorMessage
 * instances by their unique message ID, and handles finalization or dropping inside
 * onMessageResult().
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

    [[nodiscard]] bool tracksMessageResults() const noexcept override;

    void onMessageResult(
        std::string_view inputChannel,
        Common::Compat::span<const std::byte> rawMessage,
        const ::Backend::MessageResult& result) override;

    /**
     * @brief Registers an in-flight scoped message to be resolved upon handler completion.
     */
    void registerInFlight(std::uint64_t msgId, ScopedVendorMessage msg);

    /**
     * @brief Extracts and unregisters an in-flight scoped message by ID.
     */
    std::optional<ScopedVendorMessage> unregisterInFlight(std::uint64_t msgId);

    [[nodiscard]] std::shared_ptr<Sdk::VendorManager> vendorManager() const noexcept;

private:
    std::shared_ptr<Sdk::VendorManager> m_vendorManager;
    std::mutex m_mutex;
    std::unordered_map<std::uint64_t, ScopedVendorMessage> m_inFlightMessages;
};

} // namespace Backend::Vendor

