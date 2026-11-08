#pragma once

#include "backend/message/EnvelopeHeader.hpp"
#include <cstdint>

namespace Backend::Detail {

inline constexpr std::uint8_t MinSupportedEnvelopeVersion = 1;
inline constexpr std::uint8_t MaxSupportedEnvelopeVersion = 1;

std::size_t writeEnvelopeHeaderWithVersion(
    Common::Compat::span<std::byte> destination,
    ProtocolId protocol,
    std::uint16_t metadataLength,
    std::uint32_t payloadLength,
    Common::Compat::UtcTimePoint timestamp,
    std::uint8_t version) noexcept;

} // namespace Backend::Detail
