#include "backend/EnvelopeHeader.hpp"
#include "common/io/WireFormat.hpp"

namespace Backend {

std::size_t writeEnvelopeHeader(
    Common::Compat::span<std::byte> destination,
    ProtocolId protocol,
    std::uint16_t metadataLength,
    std::uint32_t payloadLength,
    std::optional<Common::Compat::UtcTimePoint> timestamp) noexcept
{
    const std::uint64_t ts = timestamp.has_value()
        ? Common::Compat::toEpochNs(*timestamp)
        : Common::Compat::toEpochNs(Common::Compat::nowUtc());

    return writeEnvelopeHeader(destination, protocol, metadataLength, payloadLength, ts);
}

std::size_t writeEnvelopeHeader(
    Common::Compat::span<std::byte> destination,
    ProtocolId protocol,
    std::uint16_t metadataLength,
    std::uint32_t payloadLength,
    std::uint64_t timestampNs) noexcept
{
    if (destination.size() < sizeof(EnvelopeHeader) || destination.data() == nullptr) {
        return 0;
    }

    std::byte* dst = destination.data();
    Common::IO::Wire::writeUint16Be(dst, EnvelopeMagic);
    dst[2] = static_cast<std::byte>(EnvelopeVersion);
    dst[3] = static_cast<std::byte>(protocol.value());
    Common::IO::Wire::writeUint16Be(dst + 4, metadataLength);
    Common::IO::Wire::writeUint32Be(dst + 6, payloadLength);
    Common::IO::Wire::writeUint64Be(dst + 10, timestampNs);

    return sizeof(EnvelopeHeader);
}

} // namespace Backend
