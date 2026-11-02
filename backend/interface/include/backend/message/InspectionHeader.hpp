#pragma once

#include <cstdint>

namespace Backend {

inline constexpr std::uint64_t PipelineInspectionMagic = 0x50585F494E535043ULL; // "PX_INSPC"

enum class InspectionVerdict : std::uint32_t {
    Pending = 0,
    Pass = 1,
    Reject = 2,
    Error = 3
};

#pragma pack(push, 1)
/**
 * @brief Zero-overhead pipeline metadata prefix tunneling original headers.
 *
 * Prepended to the message's metadata section during scatter-gather inspection.
 * Leaves EnvelopeHeader::protocol unmodified so customer/application protocol
 * identifiers are never shadowed or corrupted.
 */
struct InspectionHeader {
    std::uint64_t magic { PipelineInspectionMagic }; ///< 64-bit magic ("PX_INSPC")
    std::uint64_t correlationId { 0 }; ///< Unique ID correlating all splits of a single packet
    std::uint16_t totalSplits { 1 }; ///< Total number of parallel splits/inspectors expected
    std::uint16_t splitIndex { 0 }; ///< 0-based index of this specific inspection split
    InspectionVerdict verdict { InspectionVerdict::Pending }; ///< Inspection result verdict
    std::uint32_t errorCode { 0 }; ///< Optional error or rule violation code
    std::uint16_t originalMetadataLength { 0 }; ///< Length of customer/transport metadata following this header
};
#pragma pack(pop)

static_assert(sizeof(InspectionHeader) == 30, "InspectionHeader must be packed to exactly 30 bytes");

} // namespace Backend
