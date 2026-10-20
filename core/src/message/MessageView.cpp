#include "core/message/MessageView.hpp"

namespace Core::Message {

std::optional<MessageView> MessageView::from(std::span<const std::byte> buffer) noexcept
{
    if (buffer.size() < sizeof(EnvelopeHeader)) {
        return std::nullopt;
    }

    const auto* header = reinterpret_cast<const EnvelopeHeader*>(buffer.data());
    if (header->magic != EnvelopeMagic || header->version != EnvelopeVersion) {
        return std::nullopt;
    }

    std::size_t requiredSize = sizeof(EnvelopeHeader) + header->metadataLength + header->payloadLength;
    if (buffer.size() < requiredSize) {
        return std::nullopt;
    }

    return MessageView(buffer.subspan(0, requiredSize), header);
}

MessageView::MessageView(std::span<const std::byte> rawBuffer, const EnvelopeHeader* header) noexcept
    : m_rawBuffer(rawBuffer)
    , m_header(header)
{
    if (m_header) {
        const std::byte* base = m_rawBuffer.data() + sizeof(EnvelopeHeader);
        m_metadata = std::span<const std::byte>(base, m_header->metadataLength);
        m_payload = std::span<const std::byte>(base + m_header->metadataLength, m_header->payloadLength);
    }
}

std::span<const std::byte> MessageView::rawBuffer() const noexcept
{
    return m_rawBuffer;
}

std::span<const std::byte> MessageView::payload() const noexcept
{
    return m_payload;
}

std::span<const std::byte> MessageView::metadata() const noexcept
{
    return m_metadata;
}

ProtocolId MessageView::protocol() const noexcept
{
    return m_header ? m_header->protocol : ProtocolId::Raw;
}

std::uint64_t MessageView::timestampNs() const noexcept
{
    return m_header ? m_header->timestampNs : 0;
}

} // namespace Core::Message
