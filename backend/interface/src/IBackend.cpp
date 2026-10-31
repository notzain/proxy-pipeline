#include "backend/IBackend.hpp"
#include <utility>

namespace Backend {

bool IBackend::tracksMessageResults() const noexcept
{
    return false;
}

void IBackend::onMessageResult(
    std::string_view,
    Common::Compat::span<const std::byte>,
    const MessageResult&)
{
}

void IBackend::onHandlerStarted()
{
}

void IBackend::setBufferPool(std::shared_ptr<Common::Memory::BufferPool> pool) noexcept
{
    m_bufferPool = std::move(pool);
}

std::shared_ptr<Common::Memory::BufferPool> IBackend::bufferPool() const
{
    if (!m_bufferPool) {
        m_bufferPool = std::make_shared<Common::Memory::BufferPool>();
    }
    return m_bufferPool;
}

} // namespace Backend
