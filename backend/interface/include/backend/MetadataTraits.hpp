#pragma once

#include "backend/ProtocolId.hpp"

namespace Backend {

/**
 * @brief Compile-time traits mapping a strongly-typed metadata struct `T` to its authoritative `ProtocolId`.
 *
 * Specialize `MetadataTraits<T>` for each protocol metadata structure so `MessageView::getMetadata<T>()`
 * validates both the message's `ProtocolId` and exact byte size before deserializing.
 */
template <typename T>
struct MetadataTraits {
    static constexpr bool isRegistered = false;
};

} // namespace Backend

