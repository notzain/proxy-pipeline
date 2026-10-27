#include "common/memory/BufferSlice.hpp"
#include "common/memory/BufferPool.hpp"

namespace Common::Memory {

BufferSlice::BufferSlice() noexcept
    : m_data(nullptr)
    , m_size(0)
    , m_blockIndex(0)
    , m_pool(nullptr)
{
}

BufferSlice::BufferSlice(std::byte* data, std::size_t size, std::size_t blockIndex, std::shared_ptr<BufferPool> pool) noexcept
    : m_data(data)
    , m_size(size)
    , m_blockIndex(blockIndex)
    , m_pool(std::move(pool))
{
}

BufferSlice::~BufferSlice()
{
    reset();
}

BufferSlice::BufferSlice(BufferSlice&& other) noexcept
    : m_data(other.m_data)
    , m_size(other.m_size)
    , m_blockIndex(other.m_blockIndex)
    , m_pool(std::move(other.m_pool))
{
    other.m_data = nullptr;
    other.m_size = 0;
    other.m_blockIndex = 0;
}

BufferSlice& BufferSlice::operator=(BufferSlice&& other) noexcept
{
    if (this != &other) {
        reset();
        m_data = other.m_data;
        m_size = other.m_size;
        m_blockIndex = other.m_blockIndex;
        m_pool = std::move(other.m_pool);

        other.m_data = nullptr;
        other.m_size = 0;
        other.m_blockIndex = 0;
    }
    return *this;
}

Common::Compat::span<std::byte> BufferSlice::span() noexcept
{
    return Common::Compat::span<std::byte>(m_data, m_size);
}

Common::Compat::span<const std::byte> BufferSlice::span() const noexcept
{
    return Common::Compat::span<const std::byte>(m_data, m_size);
}

std::byte* BufferSlice::data() noexcept
{
    return m_data;
}

const std::byte* BufferSlice::data() const noexcept
{
    return m_data;
}

std::size_t BufferSlice::size() const noexcept
{
    return m_size;
}

bool BufferSlice::empty() const noexcept
{
    return m_size == 0;
}

BufferSlice::operator bool() const noexcept
{
    return m_data != nullptr;
}

void BufferSlice::resize(std::size_t newSize) noexcept
{
    m_size = newSize;
}

void BufferSlice::reset() noexcept
{
    if (m_pool && m_data) {
        m_pool->release(m_blockIndex);
    }
    m_data = nullptr;
    m_size = 0;
    m_blockIndex = 0;
    m_pool.reset();
}

} // namespace Common::Memory
