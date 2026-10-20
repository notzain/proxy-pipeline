#include "core/message/MessageEnvelope.hpp"
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
    std::span<std::byte> destination,
    ProtocolId protocol,
    std::span<const std::byte> metadata,
    std::span<const std::byte> payload,
    std::uint64_t timestampNs)
{
    std::size_t totalSize = sizeof(EnvelopeHeader) + metadata.size() + payload.size();
    if (destination.data() == nullptr || destination.size() < totalSize) {
        return 0;
    }

    EnvelopeHeader header { };
    header.magic = EnvelopeMagic;
    header.version = EnvelopeVersion;
    header.protocol = protocol;
    header.metadataLength = static_cast<std::uint16_t>(metadata.size());
    header.payloadLength = static_cast<std::uint32_t>(payload.size());
    header.timestampNs = (timestampNs != 0) ? timestampNs : getCurrentTimeNs();

    std::memcpy(destination.data(), &header, sizeof(EnvelopeHeader));
    if (!metadata.empty()) {
        std::memcpy(destination.data() + sizeof(EnvelopeHeader), metadata.data(), metadata.size());
    }
    if (!payload.empty()) {
        std::memcpy(destination.data() + sizeof(EnvelopeHeader) + metadata.size(), payload.data(), payload.size());
    }

    return totalSize;
}

std::vector<std::byte> MessageEnvelope::pack(
    ProtocolId protocol,
    std::span<const std::byte> metadata,
    std::span<const std::byte> payload,
    std::uint64_t timestampNs)
{
    std::size_t totalSize = sizeof(EnvelopeHeader) + metadata.size() + payload.size();
    std::vector<std::byte> buffer(totalSize);
    packInto(buffer, protocol, metadata, payload, timestampNs);
    return buffer;
}

std::vector<std::byte> MessageEnvelope::packRaw(
    std::span<const std::byte> payload,
    std::uint64_t timestampNs)
{
    return pack(ProtocolId::Raw, { }, payload, timestampNs);
}

std::vector<std::byte> MessageEnvelope::packUdp(
    const UdpMetadata& meta,
    std::span<const std::byte> payload,
    std::uint64_t timestampNs)
{
    std::span<const std::byte> metaSpan(
        reinterpret_cast<const std::byte*>(&meta),
        sizeof(UdpMetadata));
    return pack(ProtocolId::Udp, metaSpan, payload, timestampNs);
}

std::vector<std::byte> MessageEnvelope::withPayload(
    const MessageView& view,
    std::span<const std::byte> newPayload)
{
    return pack(view.protocol(), view.metadata(), newPayload, view.timestampNs());
}

} // namespace Core::Message
