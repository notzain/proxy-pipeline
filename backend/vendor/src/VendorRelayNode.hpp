#pragma once

#include "backend/Settlement.hpp"
#include "backend/vendor/VendorSdkStub.hpp"
#include "common/compat/Span.hpp"
#include "common/io/MappedFile.hpp"
#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace Backend::Vendor {

class VendorRelayPool;

/**
 * @brief Stationary relay node holding a single in-flight VendorMessage.
 *
 * Placed in a pre-allocated pool (VendorRelayPool). Guarantees that VendorMessage
 * remains at a stable memory location across queue handoffs without heap allocations.
 */
struct VendorRelayNode {
    Sdk::VendorMessage message;
    std::shared_ptr<Sdk::VendorManager> manager;
    std::optional<Common::Io::MappedFile> mapping;
    VendorRelayPool* pool { nullptr };
    bool consumed { false };
    std::atomic<bool> inUse { false };

    [[nodiscard]] Common::Compat::span<const std::byte> payloadView() const noexcept;

    static void settle(void* ctx, ::Backend::Settlement outcome) noexcept;
    static void recycle(void* ctx) noexcept;
};

/**
 * @brief Bounded, pre-allocated pool of stationary relay nodes owned by a single VendorInputChannel.
 */
class VendorRelayPool {
public:
    static constexpr std::size_t DefaultCapacity = 1024;

    explicit VendorRelayPool(std::size_t capacity = DefaultCapacity);

    [[nodiscard]] VendorRelayNode* acquire() noexcept;

    void release(VendorRelayNode* node) noexcept;

    [[nodiscard]] std::size_t capacity() const noexcept;

private:
    std::vector<VendorRelayNode> m_nodes;
};

} // namespace Backend::Vendor
