#pragma once

#include "common/compat/Span.hpp"
#include <cstddef>
#include <optional>
#include <string>

namespace Common::Io {

/**
 * @brief Move-only RAII private (copy-on-write) memory mapping of an entire file.
 *
 * Provides zero-copy access to file contents without consuming heap or pool memory. The mapping
 * remains valid after the underlying file is unlinked, and is unmapped on destruction.
 */
class MappedFile {
public:
    MappedFile() noexcept = default;
    ~MappedFile() noexcept;

    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    /**
     * @brief Maps the file at @p path privately (read/write, copy-on-write).
     *
     * @return The mapping, or std::nullopt if the file cannot be opened, is empty, or mapping fails.
     */
    [[nodiscard]] static std::optional<MappedFile> open(const std::string& path) noexcept;

    /**
     * @brief Returns a read-only span over the mapped bytes.
     */
    [[nodiscard]] Common::Compat::span<const std::byte> span() const noexcept;

    /**
     * @brief Returns a mutable span over the mapped bytes (writes are private to this process).
     */
    [[nodiscard]] Common::Compat::span<std::byte> span() noexcept;

    /**
     * @brief Returns the number of mapped bytes.
     */
    [[nodiscard]] std::size_t size() const noexcept;

private:
    MappedFile(std::byte* data, std::size_t size) noexcept;
    void reset() noexcept;

    std::byte* m_data { nullptr };
    std::size_t m_size { 0 };
};

} // namespace Common::Io
