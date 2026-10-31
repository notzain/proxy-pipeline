#include "backend/message/EnvelopeHeader.hpp"
#include "common/io/WireFormat.hpp"

namespace Backend {

std::size_t writeEnvelopeHeader(
    Common::Compat::span<std::byte> destination,
    ProtocolId protocol,
    std::uint16_t metadataLength,
    std::uint32_t payloadLength,
    Common::Compat::UtcTimePoint timestamp) noexcept
{
    if (destination.size() < sizeof(EnvelopeHeader) || destination.data() == nullptr) {
        return 0;
    }

    const std::uint64_t ts = Common::Compat::toEpochNs(timestamp);

    std::byte* dst = destination.data();
    Common::IO::Wire::writeUint16Be(dst, EnvelopeMagic);
    dst[2] = static_cast<std::byte>(EnvelopeVersion);
    dst[3] = static_cast<std::byte>(protocol.value());
    Common::IO::Wire::writeUint16Be(dst + 4, metadataLength);
    Common::IO::Wire::writeUint32Be(dst + 6, payloadLength);
    Common::IO::Wire::writeUint64Be(dst + 10, ts);

    return sizeof(EnvelopeHeader);
}

} // namespace Backend
