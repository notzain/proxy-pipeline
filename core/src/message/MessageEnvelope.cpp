#include "core/message/MessageEnvelope.hpp"
#include "common/memory/ByteView.hpp"
#include "core/message/WireFormat.hpp"
#include <chrono>
#include <cstring>

namespace Core::Message {

namespace {

    std::uint64_t getCurrentTimeNs()
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
    }

} // namespace

std::size_t MessageEnvelope::packInto(
    Common::Compat::span<std::byte> destination,
    ProtocolId protocol,
    Common::Compat::span<const std::byte> metadata,
    Common::Compat::span<const std::byte> payload,
    std::uint64_t timestampNs)
{
    std::size_t totalSize = sizeof(EnvelopeHeader) + metadata.size() + payload.size();
    if (destination.data() == nullptr || destination.size() < totalSize) {
        return 0;
    }

    std::byte* dst = destination.data();
    Wire::writeUint16Be(dst, EnvelopeMagic);
    dst[2] = static_cast<std::byte>(EnvelopeVersion);
    dst[3] = static_cast<std::byte>(protocol);
    Wire::writeUint16Be(dst + 4, static_cast<std::uint16_t>(metadata.size()));
    Wire::writeUint32Be(dst + 6, static_cast<std::uint32_t>(payload.size()));

    std::uint64_t ts = (timestampNs != 0) ? timestampNs : getCurrentTimeNs();
    Wire::writeUint64Be(dst + 10, ts);

    if (!metadata.empty()) {
        std::memcpy(dst + sizeof(EnvelopeHeader), metadata.data(), metadata.size());
    }
    if (!payload.empty()) {
        std::memcpy(dst + sizeof(EnvelopeHeader) + metadata.size(), payload.data(), payload.size());
    }

    return totalSize;
}

std::vector<std::byte> MessageEnvelope::pack(
    ProtocolId protocol,
    Common::Compat::span<const std::byte> metadata,
    Common::Compat::span<const std::byte> payload,
    std::uint64_t timestampNs)
{
    std::size_t totalSize = sizeof(EnvelopeHeader) + metadata.size() + payload.size();
    std::vector<std::byte> buffer(totalSize);
    packInto(buffer, protocol, metadata, payload, timestampNs);
    return buffer;
}

std::vector<std::byte> MessageEnvelope::packRaw(
    Common::Compat::span<const std::byte> payload,
    std::uint64_t timestampNs)
{
    return pack(ProtocolId::Raw, { }, payload, timestampNs);
}

std::vector<std::byte> MessageEnvelope::packUdp(
    const UdpMetadata& meta,
    Common::Compat::span<const std::byte> payload,
    std::uint64_t timestampNs)
{
    return pack(ProtocolId::Udp, Common::Memory::ByteView(meta), payload, timestampNs);
}

std::vector<std::byte> MessageEnvelope::withPayload(
    const MessageView& view,
    Common::Compat::span<const std::byte> newPayload)
{
    return pack(view.protocol(), view.metadata(), newPayload, view.timestampNs());
}

} // namespace Core::Message
