#pragma once

#include "backend/ILeaseState.hpp"
#include "backend/vendor/VendorSdkStub.hpp"
#include "common/compat/Span.hpp"
#include "common/memory/DynamicBuffer.hpp"
#include <cstddef>
#include <memory>

namespace Backend::Vendor {

/**
 * @brief Transport-specific lease state owning a single in-flight VendorMessage.
 *
 * Holds the vendor message, any backing buffer allocated for file-based payloads (Mode B),
 * and a shared reference to the VendorManager, settling the message directly via finalize()
 * or drop() without any backend registry.
 */
class VendorLeaseState final : public ::Backend::ILeaseState {
public:
    VendorLeaseState(
        Sdk::VendorMessage message,
        std::shared_ptr<Sdk::VendorManager> manager,
        Common::Memory::DynamicBuffer buffer = { }) noexcept;

    ~VendorLeaseState() override = default;

    void settle(::Backend::Settlement outcome) noexcept override;

    /**
     * @brief Returns a non-owning span over the message payload (either in-memory Mode A or loaded file Mode B).
     */
    [[nodiscard]] Common::Compat::span<const std::byte> payloadView() const noexcept;

    /**
     * @brief Returns the owned VendorMessage for direct (consuming) forwarding.
     *
     * The message and any pooled/mapped file buffer stay alive in place so MessageView's zero-copy
     * payload span remains valid for subsequent fan-out channels.
     */
    [[nodiscard]] const Sdk::VendorMessage& message() const noexcept;

    /**
     * @brief Returns true if this lease was produced by @p manager (forwarding is only valid within one manager).
     */
    [[nodiscard]] bool belongsTo(const Sdk::VendorManager& manager) const noexcept;

    /**
     * @brief Marks the message as consumed by an output channel so settlement skips finalize()/drop().
     *
     * Must only be called after the vendor sender has successfully accepted the message.
     */
    void markConsumed() noexcept;

    /**
     * @brief Returns true if the message has already been consumed by an output channel.
     */
    [[nodiscard]] bool isConsumed() const noexcept;

private:
    Sdk::VendorMessage m_message;
    std::shared_ptr<Sdk::VendorManager> m_manager;
    Common::Memory::DynamicBuffer m_buffer;
    bool m_consumed { false };
};

} // namespace Backend::Vendor
