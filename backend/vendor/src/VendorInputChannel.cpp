#include "backend/vendor/VendorInputChannel.hpp"
#include "VendorRelayNode.hpp"
#include "backend/memory/SettlementToken.hpp"
#include "backend/vendor/VendorBackend.hpp"
#include "common/io/MappedFile.hpp"
#include "log/Log.hpp"
#include <filesystem>
#include <system_error>
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
    , m_relayPool(std::make_unique<VendorRelayPool>(capacity))
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
    return m_relayPool ? m_relayPool->capacity() : 0;
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

    Sdk::VendorMessage rawMsg;
    if (!m_receiver->receive(&rawMsg, static_cast<int>(timeout.count()))) {
        return Common::Net::BytesResult::err(Common::Net::ReceiveTimeout { });
    }

    // Acquire a stationary relay node from the channel's private pool (zero heap allocation on hot path)
    VendorRelayNode* node = m_relayPool ? m_relayPool->acquire() : nullptr;
    if (!node) {
        Proxy::Log::error("[VendorInputChannel] Relay pool exhausted for channel '{}', dropping message ID {}",
            name(), rawMsg.getId());
        m_manager->drop(rawMsg);
        return Common::Net::BytesResult::err(Common::Net::ReceiveFailure { });
    }

    node->message = std::move(rawMsg);
    node->manager = m_manager;
    node->consumed = false;
    node->pool = m_relayPool.get();

    const std::string& filePath = node->message.getPayloadFilePath();
    if (!filePath.empty()) {
        std::error_code ec;
        const auto fileSize = static_cast<std::size_t>(std::filesystem::file_size(filePath, ec));
        if (ec) {
            Proxy::Log::error("[VendorInputChannel] Failed to read payload file '{}' for message ID {}",
                filePath, node->message.getId());
            m_manager->drop(node->message);
            m_relayPool->release(node);
            return Common::Net::BytesResult::err(Common::Net::ReceiveFailure { });
        }
        if (fileSize > 0) {
            node->mapping = Common::Io::MappedFile::open(filePath);
            if (!node->mapping) {
                Proxy::Log::error("[VendorInputChannel] Failed to map payload file '{}' for message ID {}",
                    filePath, node->message.getId());
                m_manager->drop(node->message);
                m_relayPool->release(node);
                return Common::Net::BytesResult::err(Common::Net::ReceiveFailure { });
            }
        }
    }

    const auto view = node->payloadView();

    // Construct zero-allocation SettlementToken with stateless static function pointers
    auto token = ::Backend::SettlementToken(
        node,
        &VendorRelayNode::settle,
        &VendorRelayNode::recycle);

    slot.assignLeased(view, ::Backend::Lease(std::move(token)));
    return Common::Net::BytesResult::ok(slot.size);
}

} // namespace Backend::Vendor
