#include "backend/message/MessageView.hpp"
#include "backend/message/EnvelopeHeader.hpp"
#include "common/io/WireFormat.hpp"
#include <chrono>
#include <cstring>
#include <limits>

namespace Backend {

namespace {

    bool isFrameSizeValid(std::size_t metadataSize, std::size_t payloadSize) noexcept
    {
        if (metadataSize > std::numeric_limits<std::uint16_t>::max()
            || payloadSize > std::numeric_limits<std::uint32_t>::max()
            || metadataSize > MaxEnvelopeFrameBytes - sizeof(EnvelopeHeader)) {
            return false;
        }

        return payloadSize <= MaxEnvelopeFrameBytes - sizeof(EnvelopeHeader) - metadataSize;
    }

} // namespace

std::size_t MessageView::packInto(
    Common::Compat::span<std::byte> destination,
    ProtocolId protocol,
    Common::Compat::span<const std::byte> metadata,
    Common::Compat::span<const std::byte> payload,
    std::uint64_t timestampNs)
{
    if (!isFrameSizeValid(metadata.size(), payload.size())) {
        return 0;
    }

    const std::size_t totalSize = sizeof(EnvelopeHeader) + metadata.size() + payload.size();
    if (destination.data() == nullptr || destination.size() < totalSize) {
        return 0;
    }

    if (timestampNs != 0) {
        writeEnvelopeHeader(
            destination,
            protocol,
            static_cast<std::uint16_t>(metadata.size()),
            static_cast<std::uint32_t>(payload.size()),
            Common::Compat::fromEpochNs(timestampNs));
    } else {
        writeEnvelopeHeader(
            destination,
            protocol,
            static_cast<std::uint16_t>(metadata.size()),
            static_cast<std::uint32_t>(payload.size()));
    }

    std::byte* dst = destination.data();
    if (!metadata.empty()) {
        std::memcpy(dst + sizeof(EnvelopeHeader), metadata.data(), metadata.size());
    }
    if (!payload.empty()) {
        std::memcpy(dst + sizeof(EnvelopeHeader) + metadata.size(), payload.data(), payload.size());
    }

    return totalSize;
}

std::optional<std::vector<std::byte>> MessageView::pack(
    ProtocolId protocol,
    Common::Compat::span<const std::byte> metadata,
    Common::Compat::span<const std::byte> payload,
    std::uint64_t timestampNs)
{
    if (!isFrameSizeValid(metadata.size(), payload.size())) {
        return std::nullopt;
    }

    std::size_t totalSize = sizeof(EnvelopeHeader) + metadata.size() + payload.size();
    std::vector<std::byte> buffer(totalSize);
    if (packInto(buffer, protocol, metadata, payload, timestampNs) == 0) {
        return std::nullopt;
    }
    return buffer;
}

std::optional<std::vector<std::byte>> MessageView::packRaw(
    Common::Compat::span<const std::byte> payload,
    std::uint64_t timestampNs)
{
    return pack(RawProtocol, { }, payload, timestampNs);
}

std::optional<std::vector<std::byte>> MessageView::withPayload(
    const MessageView& view,
    Common::Compat::span<const std::byte> newPayload)
{
    return pack(view.protocol(), view.metadata(), newPayload, view.timestampNs());
}

MessageView MessageView::withPayload(Common::Compat::span<const std::byte> newPayload) const noexcept
{
    MessageView rebound({ }, m_metadata, newPayload, m_protocol, m_timestampNs);
    rebound.m_lease = m_lease;
    return rebound;
}

bool MessageView::hasWireEnvelope() const noexcept
{
    return m_rawBuffer.size() >= sizeof(EnvelopeHeader) + m_metadata.size() + m_payload.size()
        && m_rawBuffer.data() != m_payload.data();
}

std::optional<MessageView> MessageView::from(Common::Compat::span<const std::byte> buffer) noexcept
{
    if (buffer.size() < sizeof(EnvelopeHeader) || buffer.size() > MaxEnvelopeFrameBytes) {
        return std::nullopt;
    }

    const std::byte* data = buffer.data();
    std::uint16_t magic = Common::IO::Wire::readUint16Be(data);
    std::uint8_t version = static_cast<std::uint8_t>(data[2]);
    if (magic != EnvelopeMagic || version != EnvelopeVersion) {
        return std::nullopt;
    }

    ProtocolId protocol { static_cast<std::uint8_t>(data[3]) };
    std::uint16_t metadataLength = Common::IO::Wire::readUint16Be(data + 4);
    std::uint32_t payloadLength = Common::IO::Wire::readUint32Be(data + 6);
    std::uint64_t timestampNs = Common::IO::Wire::readUint64Be(data + 10);

    if (!isFrameSizeValid(metadataLength, payloadLength)) {
        return std::nullopt;
    }

    const std::size_t requiredSize = sizeof(EnvelopeHeader) + metadataLength + payloadLength;
    if (buffer.size() != requiredSize) {
        return std::nullopt;
    }

    const std::byte* base = data + sizeof(EnvelopeHeader);
    auto frameSpan = buffer.subspan(0, requiredSize);
    auto metadataSpan = Common::Compat::span<const std::byte>(base, metadataLength);
    auto payloadSpan = Common::Compat::span<const std::byte>(base + metadataLength, payloadLength);

    return MessageView(frameSpan, metadataSpan, payloadSpan, protocol, timestampNs);
}

MessageView::MessageView(
    Common::Compat::span<const std::byte> payload,
    Common::Compat::span<const std::byte> rawBuffer,
    std::uint64_t timestampNs,
    ProtocolId protocol,
    Common::Compat::span<const std::byte> metadata) noexcept
    : m_rawBuffer(rawBuffer.empty() ? payload : rawBuffer)
    , m_metadata(metadata)
    , m_payload(payload)
    , m_timestampNs(timestampNs)
    , m_protocol(protocol)
{
}

MessageView::MessageView(
    Common::Compat::span<const std::byte> rawBuffer,
    Common::Compat::span<const std::byte> metadata,
    Common::Compat::span<const std::byte> payload,
    ProtocolId protocol,
    std::uint64_t timestampNs) noexcept
    : m_rawBuffer(rawBuffer)
    , m_metadata(metadata)
    , m_payload(payload)
    , m_timestampNs(timestampNs)
    , m_protocol(protocol)
{
}

Common::Compat::span<const std::byte> MessageView::payload() const noexcept
{
    return m_payload;
}

Common::Compat::span<const std::byte> MessageView::rawBuffer() const noexcept
{
    return m_rawBuffer;
}

Common::Compat::span<const std::byte> MessageView::metadata() const noexcept
{
    return m_metadata;
}

ProtocolId MessageView::protocol() const noexcept
{
    return m_protocol;
}

std::uint64_t MessageView::timestampNs() const noexcept
{
    return m_timestampNs;
}

void MessageView::attachLease(Common::Memory::Borrow<Lease> lease) noexcept
{
    m_lease = lease;
}

Common::Memory::ObserverPtr<SettlementToken> MessageView::token() const noexcept
{
    if (!m_lease) {
        return { };
    }
    return m_lease->token();
}

} // namespace Backend
