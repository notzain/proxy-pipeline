#include "backend/message/EnvelopeHeader.hpp"
#include "EnvelopeHeaderInternal.hpp"
#include "common/io/WireFormat.hpp"

namespace Backend {

namespace Detail {

    std::size_t writeEnvelopeHeaderWithVersion(
        Common::Compat::span<std::byte> destination,
        ProtocolId protocol,
        std::uint16_t metadataLength,
        std::uint32_t payloadLength,
        Common::Compat::UtcTimePoint timestamp,
        std::uint8_t version) noexcept
    {
        if (destination.size() < sizeof(EnvelopeHeader) || destination.data() == nullptr) {
            return 0;
        }

        const std::uint64_t ts = Common::Compat::toEpochNs(timestamp);

        std::byte* dst = destination.data();
        Common::IO::Wire::writeUint16Be(dst, EnvelopeMagic);
        dst[2] = static_cast<std::byte>(version);
        dst[3] = static_cast<std::byte>(protocol.value());
        Common::IO::Wire::writeUint16Be(dst + 4, metadataLength);
        Common::IO::Wire::writeUint32Be(dst + 6, payloadLength);
        Common::IO::Wire::writeUint64Be(dst + 10, ts);

        return sizeof(EnvelopeHeader);
    }

} // namespace Detail

std::size_t writeEnvelopeHeader(
    Common::Compat::span<std::byte> destination,
    ProtocolId protocol,
    std::uint16_t metadataLength,
    std::uint32_t payloadLength,
    Common::Compat::UtcTimePoint timestamp) noexcept
{
    return Detail::writeEnvelopeHeaderWithVersion(
        destination,
        protocol,
        metadataLength,
        payloadLength,
        timestamp,
        EnvelopeVersion);
}

} // namespace Backend
