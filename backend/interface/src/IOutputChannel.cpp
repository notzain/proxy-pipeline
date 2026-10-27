#include "backend/IOutputChannel.hpp"
#include <utility>

namespace Backend {

IOutputChannel::IOutputChannel(std::string name)
    : m_name(std::move(name))
{
}

std::string_view IOutputChannel::name() const noexcept
{
    return m_name;
}

std::size_t IOutputChannel::sendBatch(Common::Compat::span<const Common::Compat::span<const std::byte>> messages)
{
    std::size_t count = 0;
    for (const auto& msg : messages) {
        if (send(msg)) {
            ++count;
        }
    }
    return count;
}

} // namespace Backend
