#pragma once

#include "common/types/StrongType.hpp"
#include <cstdint>
#include <functional>

namespace Backend {

struct ProtocolIdTag { };

/**
 * @brief Strongly-typed 8-bit protocol identifier (e.g. IANA Assigned Internet Protocol Numbers).
 *
 * Open strong type wrapping `std::uint8_t` so applications and transport metadata definitions
 * can construct any `ProtocolId(number)` directly in `MetadataTraits<T>` without modifying
 * a centralized enum in `backend/interface`.
 */
using ProtocolId = Common::Types::StrongType<std::uint8_t, ProtocolIdTag>;

/**
 * @brief Default protocol identifier (0) representing an unadorned/raw pipeline payload.
 */
inline constexpr ProtocolId RawProtocol { 0 };

} // namespace Backend

namespace std {

template <>
struct hash<Backend::ProtocolId> {
    std::size_t operator()(const Backend::ProtocolId& id) const noexcept
    {
        return std::hash<std::uint8_t> { }(id.value());
    }
};

} // namespace std
