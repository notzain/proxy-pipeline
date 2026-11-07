#include "backend/vendor/VendorInputChannel.hpp"
#include "backend/memory/SettlementToken.hpp"
#include "backend/vendor/VendorBackend.hpp"
#include "backend/vendor/VendorRelayNode.hpp"
#include "common/diagnostics/RateLimitedLogging.hpp"
#include "log/Log.hpp"
#include <utility>

namespace Backend::Vendor {

VendorInputChannel::VendorInputChannel(
    std::string name,
    std::shared_ptr<Sdk::VendorManager> manager,
    std::shared_ptr<Sdk::VendorReceiver> receiver,
    std::size_t capacity)
    : ::Backend::IInputChannel(std::move(name))
    , m_manager(std::move(manager))
    , m_receiver(std::move(receiver))
    , m_relayPool(capacity)
{
}

VendorInputChannel::VendorInputChannel(
    std::string name,
    VendorBackend& backend,
    std::shared_ptr<Sdk::VendorReceiver> receiver,
    std::size_t capacity)
    : VendorInputChannel(std::move(name), backend.vendorManager(), std::move(receiver), capacity)
{
}

VendorInputChannel::~VendorInputChannel() = default;

void VendorInputChannel::setReceiver(std::shared_ptr<Sdk::VendorReceiver> receiver)
{
    m_receiver = std::move(receiver);
}

std::size_t VendorInputChannel::capacity() const noexcept
{
    return m_relayPool.capacity();
}

bool VendorInputChannel::providesOwnMemory() const noexcept
{
    return true;
}

Common::Net::BytesResult VendorInputChannel::receiveSlot(
    ::Backend::BufferSlot& slot,
    Timeout timeout)
{
    slot.reset();

    if (!m_receiver) {
        return Common::Net::BytesResult::err(Common::Net::ReceiveClosed { });
    }

    if (!m_manager) {
        Proxy::Log::error("[VendorInputChannel] VendorManager not initialized");
        return Common::Net::BytesResult::err(Common::Net::ReceiveFailure { });
    }

    // 1. Backpressure invariant: acquire node lease BEFORE reading from receiver.
    // If pool is fully exhausted, back off immediately so incoming messages remain
    // safely queued inside the vendor transport buffer rather than being dropped.
    // Note: node is an ObjectHandle<VendorRelayNode>. If we return early on timeout
    // or error, node's destructor automatically recycles it back to the pool!
    auto node = m_relayPool.acquire();
    if (!node) {
        const auto count = m_poolExhaustionCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (Common::Diagnostics::shouldLogEventCount(count)) {
            Proxy::Log::warn("[VendorInputChannel] Relay pool capacity reached on channel '{}'; applying ingress backpressure (total events: {})",
                name(), count);
        }
        return Common::Net::BytesResult::err(Common::Net::ReceiveTimeout { });
    }

    node->pool = &m_relayPool;

    // 2. Read from vendor receiver directly into acquired node
    if (!m_receiver->receive(&node->message, static_cast<int>(timeout.count()))) {
        // Automatic RAII recycle on scope exit!
        return Common::Net::BytesResult::err(Common::Net::ReceiveTimeout { });
    }

    node->manager = m_manager;
    node->consumed = false;

    // 3. Prepare payload for zero-copy consumption (validates and maps payload file if disk-backed)
    if (!node->preparePayload()) {
        m_manager->drop(node->message);
        // Automatic RAII recycle on scope exit!
        return Common::Net::BytesResult::err(Common::Net::ReceiveFailure { });
    }

    const auto view = node->payloadView();

    // 4. Detach raw pointer from handle and hand off to zero-allocation SettlementToken
    auto token = ::Backend::SettlementToken(
        node.release(),
        &VendorRelayNode::settle,
        &VendorRelayNode::recycle);

    slot.assignLeased(view, ::Backend::Lease(std::move(token)));
    return Common::Net::BytesResult::ok(slot.size);
}

} // namespace Backend::Vendor
