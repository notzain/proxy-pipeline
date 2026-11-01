#include "backend/vendor/VendorRelayNode.hpp"
#include "log/Log.hpp"
#include <filesystem>
#include <system_error>

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

bool VendorRelayNode::preparePayload() noexcept
{
    const std::string& filePath = message.getPayloadFilePath();
    if (filePath.empty()) {
        return true;
    }

    std::error_code ec;
    const auto fileSize = static_cast<std::size_t>(std::filesystem::file_size(filePath, ec));
    if (ec) {
        Proxy::Log::error("[VendorRelayNode] Failed to read payload file '{}' for message ID {}",
            filePath, message.getId());
        return false;
    }

    if (fileSize > 0) {
        mapping = Common::Io::MappedFile::open(filePath);
        if (!mapping) {
            Proxy::Log::error("[VendorRelayNode] Failed to map payload file '{}' for message ID {}",
                filePath, message.getId());
            return false;
        }
    }
    return true;
}

void VendorRelayNode::reset() noexcept
{
    mapping.reset();
    manager.reset();
    consumed = false;
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
    if (!n) {
        return;
    }

    n->reset();

    std::unique_ptr<VendorRelayNode> node(n);
    if (node->pool) {
        node->pool->release(std::move(node));
    }
}

} // namespace Backend::Vendor
