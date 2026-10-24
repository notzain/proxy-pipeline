#include "backend/IBackend.hpp"

namespace Backend {

bool IBackend::tracksMessageResults() const noexcept
{
    return false;
}

void IBackend::onMessageResult(
    std::string_view,
    std::span<const std::byte>,
    const MessageResult&)
{
}

void IBackend::onHandlerStarted()
{
}

} // namespace Backend
