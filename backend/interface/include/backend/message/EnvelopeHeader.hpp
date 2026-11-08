#pragma once

#include "backend/message/ProtocolId.hpp"
#include "common/compat/Span.hpp"
#include "common/compat/Time.hpp"
#include <cstddef>
#include <cstdint>

namespace Backend {

inline constexpr std::uint16_t EnvelopeMagic = 0x5058; // 'PX' (Proxy eXchange)
/// Maximum total size of an enveloped frame accepted by the current ingress buffers.
inline constexpr std::size_t MaxEnvelopeFrameBytes = 65535;

namespace V1 {
    inline constexpr std::uint8_t Version = 1;

    /**
     * @brief Fixed-size, packed binary envelope header preceding every message frame (v1).
     */
#pragma pack(push, 1)
    struct EnvelopeHeader {
        std::uint16_t magic { EnvelopeMagic }; ///< Magic bytes 0x5058 ('PX')
        std::uint8_t version { Version }; ///< Wire protocol format version (1)
        ProtocolId protocol { RawProtocol }; ///< Protocol identifier
        std::uint16_t metadataLength { 0 }; ///< Size of the following metadata section in bytes
        std::uint32_t payloadLength { 0 }; ///< Size of the application payload in bytes
        std::uint64_t timestampNs { 0 }; ///< Ingress timestamp in nanoseconds since UNIX epoch
    };
#pragma pack(pop)

    static_assert(sizeof(EnvelopeHeader) == 18, "EnvelopeHeader must be exactly 18 bytes packed");
} // namespace V1

// Current canonical wire header exposed in top-level Backend namespace
using EnvelopeHeader = V1::EnvelopeHeader;
inline constexpr std::uint8_t EnvelopeVersion = V1::Version;

/**
 * @brief Serializes the standard 18-byte big-endian envelope header into @p destination using the current version.
 *
 * @param destination Target memory span. Must be at least sizeof(EnvelopeHeader) (18 bytes).
 * @param protocol Transport protocol identifier.
 * @param metadataLength Length of the metadata section in bytes.
 * @param payloadLength Length of the application payload in bytes.
 * @param timestamp UTC Unix epoch timestamp. Defaults to current UTC time.
 * @return std::size_t Number of bytes written (18), or 0 if destination is too small.
 */
std::size_t writeEnvelopeHeader(
    Common::Compat::span<std::byte> destination,
    ProtocolId protocol,
    std::uint16_t metadataLength,
    std::uint32_t payloadLength,
    Common::Compat::UtcTimePoint timestamp = Common::Compat::nowUtc()) noexcept;

} // namespace Backend
