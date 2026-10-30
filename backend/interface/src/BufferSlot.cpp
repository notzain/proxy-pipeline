#include "backend/BufferSlot.hpp"
#include <utility>

namespace Backend {

BufferSlot::BufferSlot(Common::Compat::span<std::byte> target, std::size_t filled) noexcept
    : buffer(target)
    , size(filled)
{
}

void BufferSlot::reset() noexcept
{
    size = 0;
    lease.release();
    m_leasedView = { };
    m_leased = false;
}

void BufferSlot::assignLeased(Common::Compat::span<const std::byte> view, Lease owner) noexcept
{
    lease = std::move(owner);
    m_leasedView = view;
    m_leased = true;
    size = view.size();
}

bool BufferSlot::isLeased() const noexcept
{
    return m_leased;
}

PayloadFormat BufferSlot::format() const noexcept
{
    return m_leased ? PayloadFormat::Raw : PayloadFormat::Enveloped;
}

Common::Compat::span<const std::byte> BufferSlot::filledSpan() const noexcept
{
    if (m_leased) {
        return m_leasedView;
    }
    return buffer.subspan(0, size);
}

} // namespace Backend
