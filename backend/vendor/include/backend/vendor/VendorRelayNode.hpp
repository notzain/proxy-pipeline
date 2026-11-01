#pragma once

#include "backend/memory/Settlement.hpp"
#include "backend/vendor/VendorSdkStub.hpp"
#include "common/compat/Span.hpp"
#include "common/io/MappedFile.hpp"
#include "common/memory/ObjectPool.hpp"
#include <cstddef>
#include <memory>
#include <optional>

namespace Backend::Vendor {

/**
 * @brief Stationary relay node holding a single in-flight VendorMessage.
 *
 * Managed in an ObjectPool<VendorRelayNode>. Guarantees that VendorMessage
 * remains at a stable memory location across queue handoffs without heap allocations.
 * Ownership is transferred across thread boundaries via SettlementToken.
 */
struct VendorRelayNode {
    Sdk::VendorMessage message;
    std::shared_ptr<Sdk::VendorManager> manager;
    std::optional<Common::Io::MappedFile> mapping;
    Common::Memory::ObjectPool<VendorRelayNode>* pool { nullptr };
    bool consumed { false };

    [[nodiscard]] Common::Compat::span<const std::byte> payloadView() const noexcept;

    /**
     * @brief Prepares and validates the payload storage (maps payload file if disk-backed).
     * @return true if payload is ready for consumption, false on I/O or mapping failure.
     */
    [[nodiscard]] bool preparePayload() noexcept;

    void reset() noexcept;

    static void settle(void* ctx, ::Backend::Settlement outcome) noexcept;
    static void recycle(void* ctx) noexcept;
};

} // namespace Backend::Vendor
