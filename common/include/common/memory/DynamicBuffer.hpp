#pragma once

#include "common/compat/Span.hpp"
#include "common/io/MappedFile.hpp"
#include "common/memory/BufferSlice.hpp"
#include "common/types/Variant.hpp"
#include <cstddef>
#include <variant>
#include <vector>

namespace Common::Memory {

/**
 * @brief Dynamic backing storage variant holding a pooled BufferSlice, a heap vector, a private file mapping, or empty.
 *
 * Replaces parallel BufferSlice and std::vector<std::byte> fields with a single mutually exclusive
 * storage variant that automatically reclaims previous storage upon assignment.
 */
using DynamicBuffer = Common::Types::Variant<
    std::monostate,
    BufferSlice,
    std::vector<std::byte>,
    Common::Io::MappedFile>;

/**
 * @brief Returns a non-owning read-only span over the populated bytes in a DynamicBuffer.
 */
[[nodiscard]] Common::Compat::span<const std::byte> spanOf(const DynamicBuffer& buf) noexcept;

/**
 * @brief Returns a non-owning mutable span over the populated bytes in a DynamicBuffer.
 */
[[nodiscard]] Common::Compat::span<std::byte> mutableSpanOf(DynamicBuffer& buf) noexcept;

/**
 * @brief Returns true if the DynamicBuffer holds active data.
 */
[[nodiscard]] bool hasData(const DynamicBuffer& buf) noexcept;

/**
 * @brief Returns the byte size of the data in the DynamicBuffer.
 */
[[nodiscard]] std::size_t sizeOf(const DynamicBuffer& buf) noexcept;

} // namespace Common::Memory
