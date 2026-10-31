#pragma once

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

    [[nodiscard]] Common::Compat::span<const std::byte> payloadView() const noexcept
    {
        if (mapping && mapping->size() > 0) {
            return mapping->span();
        }
        const auto bytePayload = message.getBytePayload();
        if (bytePayload.data != nullptr) {
            return Common::Compat::span<const std::byte>(bytePayload.data, bytePayload.length);
        }
        return { };
    }
};

/**
 * @brief Bounded, pre-allocated pool of stationary relay nodes owned by a single VendorInputChannel.
 */
class VendorRelayPool {
public:
    static constexpr std::size_t DefaultCapacity = 1024;

    explicit VendorRelayPool(std::size_t capacity = DefaultCapacity)
        : m_nodes(capacity > 0 ? capacity : DefaultCapacity)
    {
        for (auto& node : m_nodes) {
            node.pool = this;
        }
    }

    [[nodiscard]] VendorRelayNode* acquire() noexcept
    {
        const auto n = m_nodes.size();
        for (std::size_t i = 0; i < n; ++i) {
            bool expected = false;
            if (m_nodes[i].inUse.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                m_nodes[i].consumed = false;
                m_nodes[i].mapping.reset();
                return &m_nodes[i];
            }
        }
        return nullptr;
    }

    void release(VendorRelayNode* node) noexcept
    {
        if (node) {
            node->mapping.reset();
            node->manager.reset();
            node->consumed = false;
            node->inUse.store(false, std::memory_order_release);
        }
    }

    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return m_nodes.size();
    }

private:
    std::vector<VendorRelayNode> m_nodes;
};

} // namespace Backend::Vendor
