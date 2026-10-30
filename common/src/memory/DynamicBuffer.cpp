#include "common/memory/DynamicBuffer.hpp"

namespace Common::Memory {

Common::Compat::span<const std::byte> spanOf(const DynamicBuffer& buf) noexcept
{
    return buf.match(
        [](std::monostate) noexcept -> Common::Compat::span<const std::byte> {
            return { };
        },
        [](const BufferSlice& slice) noexcept -> Common::Compat::span<const std::byte> {
            return slice.span();
        },
        [](const std::vector<std::byte>& vec) noexcept -> Common::Compat::span<const std::byte> {
            return Common::Compat::span<const std::byte>(vec.data(), vec.size());
        },
        [](const Common::Io::MappedFile& mapping) noexcept -> Common::Compat::span<const std::byte> {
            return mapping.span();
        });
}

Common::Compat::span<std::byte> mutableSpanOf(DynamicBuffer& buf) noexcept
{
    return buf.match(
        [](std::monostate) noexcept -> Common::Compat::span<std::byte> {
            return { };
        },
        [](BufferSlice& slice) noexcept -> Common::Compat::span<std::byte> {
            return slice.span();
        },
        [](std::vector<std::byte>& vec) noexcept -> Common::Compat::span<std::byte> {
            return Common::Compat::span<std::byte>(vec.data(), vec.size());
        },
        [](Common::Io::MappedFile& mapping) noexcept -> Common::Compat::span<std::byte> {
            return mapping.span();
        });
}

bool hasData(const DynamicBuffer& buf) noexcept
{
    return !buf.is<std::monostate>();
}

std::size_t sizeOf(const DynamicBuffer& buf) noexcept
{
    return spanOf(buf).size();
}

} // namespace Common::Memory
