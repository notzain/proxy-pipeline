#include "common/io/MappedFile.hpp"
#include "common/io/UniqueFd.hpp"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <utility>

namespace Common::Io {

MappedFile::MappedFile(std::byte* data, std::size_t size) noexcept
    : m_data(data)
    , m_size(size)
{
}

MappedFile::~MappedFile() noexcept
{
    reset();
}

MappedFile::MappedFile(MappedFile&& other) noexcept
    : m_data(std::exchange(other.m_data, nullptr))
    , m_size(std::exchange(other.m_size, 0))
{
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept
{
    if (this != &other) {
        reset();
        m_data = std::exchange(other.m_data, nullptr);
        m_size = std::exchange(other.m_size, 0);
    }
    return *this;
}

std::optional<MappedFile> MappedFile::open(const std::string& path) noexcept
{
    UniqueFd fd(::open(path.c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd) {
        return std::nullopt;
    }

    struct stat st { };
    if (::fstat(fd.get(), &st) != 0 || st.st_size <= 0) {
        return std::nullopt;
    }

    const auto size = static_cast<std::size_t>(st.st_size);
    void* addr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd.get(), 0);
    if (addr == MAP_FAILED) {
        return std::nullopt;
    }
    return MappedFile(static_cast<std::byte*>(addr), size);
}

Common::Compat::span<const std::byte> MappedFile::span() const noexcept
{
    return { m_data, m_size };
}

Common::Compat::span<std::byte> MappedFile::span() noexcept
{
    return { m_data, m_size };
}

std::size_t MappedFile::size() const noexcept
{
    return m_size;
}

void MappedFile::reset() noexcept
{
    if (m_data != nullptr) {
        ::munmap(m_data, m_size);
    }
    m_data = nullptr;
    m_size = 0;
}

} // namespace Common::Io
