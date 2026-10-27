#include "core/message/MessageView.hpp"
#include "core/message/EnvelopeHeader.hpp"
#include "core/message/WireFormat.hpp"

namespace Core::Message {

std::optional<MessageView> MessageView::from(Common::Compat::span<const std::byte> buffer) noexcept
{
    if (buffer.size() < sizeof(EnvelopeHeader)) {
        return std::nullopt;
    }

    const std::byte* data = buffer.data();
    std::uint16_t magic = Wire::readUint16Be(data);
    std::uint8_t version = static_cast<std::uint8_t>(data[2]);
    if (magic != EnvelopeMagic || version != EnvelopeVersion) {
        return std::nullopt;
    }

    auto protocol = static_cast<ProtocolId>(data[3]);
    std::uint16_t metadataLength = Wire::readUint16Be(data + 4);
    std::uint32_t payloadLength = Wire::readUint32Be(data + 6);
    std::uint64_t timestampNs = Wire::readUint64Be(data + 10);

    std::size_t requiredSize = sizeof(EnvelopeHeader) + metadataLength + payloadLength;
    if (buffer.size() < requiredSize) {
        return std::nullopt;
    }

    const std::byte* base = data + sizeof(EnvelopeHeader);
    Common::Compat::span<const std::byte> metadataSpan(base, metadataLength);
    Common::Compat::span<const std::byte> payloadSpan(base + metadataLength, payloadLength);
    Common::Compat::span<const std::byte> frameSpan = buffer.subspan(0, requiredSize);

    return MessageView(frameSpan, metadataSpan, payloadSpan, protocol, timestampNs);
}

MessageView::MessageView(Common::Compat::span<const std::byte> rawBuffer,
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

MessageView::MessageView(Common::Compat::span<const std::byte> payload,
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

Common::Compat::span<const std::byte> MessageView::rawBuffer() const noexcept
{
    return m_rawBuffer;
}

Common::Compat::span<const std::byte> MessageView::payload() const noexcept
{
    return m_payload;
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

} // namespace Core::Message
