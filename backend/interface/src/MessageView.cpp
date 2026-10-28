#include "backend/MessageView.hpp"
#include "common/io/WireFormat.hpp"
#include "backend/EnvelopeHeader.hpp"

namespace Backend {

std::optional<MessageView> MessageView::from(Common::Compat::span<const std::byte> buffer) noexcept
{
    if (buffer.size() < sizeof(EnvelopeHeader)) {
        return std::nullopt;
    }

    const std::byte* data = buffer.data();
    std::uint16_t magic = Common::IO::Wire::readUint16Be(data);
    std::uint8_t version = static_cast<std::uint8_t>(data[2]);
    if (magic != EnvelopeMagic || version != EnvelopeVersion) {
        return std::nullopt;
    }

    auto protocol = static_cast<ProtocolId>(data[3]);
    std::uint16_t metadataLength = Common::IO::Wire::readUint16Be(data + 4);
    std::uint32_t payloadLength = Common::IO::Wire::readUint32Be(data + 6);
    std::uint64_t timestampNs = Common::IO::Wire::readUint64Be(data + 10);

    std::size_t requiredSize = sizeof(EnvelopeHeader) + metadataLength + payloadLength;
    if (buffer.size() < requiredSize) {
        return std::nullopt;
    }

    const std::byte* base = data + sizeof(EnvelopeHeader);
    auto frameSpan = buffer.subspan(0, requiredSize);
    auto metadataSpan = Common::Compat::span<const std::byte>(base, metadataLength);
    auto payloadSpan = Common::Compat::span<const std::byte>(base + metadataLength, payloadLength);

    return MessageView(frameSpan, metadataSpan, payloadSpan, protocol, timestampNs);
}

} // namespace Backend

