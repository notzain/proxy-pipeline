#include "VendorRelayNode.hpp"
#include "log/Log.hpp"

namespace Backend::Vendor {

Common::Compat::span<const std::byte> VendorRelayNode::payloadView() const noexcept
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

void VendorRelayNode::settle(void* ctx, ::Backend::Settlement outcome) noexcept
{
    auto* n = static_cast<VendorRelayNode*>(ctx);
    if (!n || n->consumed || !n->manager) {
        return;
    }
    const std::uint64_t msgId = n->message.getId();
    switch (outcome) {
    case ::Backend::Settlement::Ack:
        Proxy::Log::debug("[VendorSettlement] Finalizing message ID {}", msgId);
        n->manager->finalize(n->message);
        break;
    case ::Backend::Settlement::Reject:
    case ::Backend::Settlement::Release:
        Proxy::Log::debug("[VendorSettlement] Dropping message ID {}", msgId);
        n->manager->drop(n->message);
        break;
    }
}

void VendorRelayNode::recycle(void* ctx) noexcept
{
    auto* n = static_cast<VendorRelayNode*>(ctx);
    if (n && n->pool) {
        n->pool->release(n);
    }
}

VendorRelayPool::VendorRelayPool(std::size_t capacity)
    : m_nodes(capacity > 0 ? capacity : DefaultCapacity)
{
    for (auto& node : m_nodes) {
        node.pool = this;
    }
}

VendorRelayNode* VendorRelayPool::acquire() noexcept
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

void VendorRelayPool::release(VendorRelayNode* node) noexcept
{
    if (node) {
        node->mapping.reset();
        node->manager.reset();
        node->consumed = false;
        node->inUse.store(false, std::memory_order_release);
    }
}

std::size_t VendorRelayPool::capacity() const noexcept
{
    return m_nodes.size();
}

} // namespace Backend::Vendor
