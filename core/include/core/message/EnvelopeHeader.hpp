#pragma once

#include "core/message/ProtocolId.hpp"
#include <cstdint>

namespace Core::Message {

inline constexpr std::uint16_t EnvelopeMagic = 0x5058; // 'PX' (Proxy eXchange)
inline constexpr std::uint8_t EnvelopeVersion = 1;

/**
 * @brief Fixed-size, packed binary envelope header preceding every message frame.
 */
#pragma pack(push, 1)
struct EnvelopeHeader {
    std::uint16_t magic { EnvelopeMagic }; ///< Magic bytes 0x5058 ('PX')
    std::uint8_t version { EnvelopeVersion }; ///< Wire protocol format version (1)
    ProtocolId protocol { ProtocolId::Raw }; ///< Protocol identifier
    std::uint16_t metadataLength { 0 }; ///< Size of the following metadata section in bytes
    std::uint32_t payloadLength { 0 }; ///< Size of the application payload in bytes
    std::uint64_t timestampNs { 0 }; ///< Ingress timestamp in nanoseconds since UNIX epoch
};
#pragma pack(pop)

static_assert(sizeof(EnvelopeHeader) == 18, "EnvelopeHeader must be exactly 18 bytes packed");

} // namespace Core::Message
