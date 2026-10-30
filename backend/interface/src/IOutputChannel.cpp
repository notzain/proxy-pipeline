#include "backend/IOutputChannel.hpp"
#include "backend/MessageView.hpp"
#include "common/memory/SmallSpanBuffer.hpp"
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

bool IOutputChannel::send(const MessageView& message)
{
    return send(message.rawBuffer());
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

std::size_t IOutputChannel::sendBatch(Common::Compat::span<const MessageView> messages)
{
    if (messages.empty()) {
        return 0;
    }

    Common::Memory::SmallSpanBuffer spans(messages.size());
    for (std::size_t i = 0; i < messages.size(); ++i) {
        spans[i] = messages[i].rawBuffer();
    }
    return sendBatch(spans.span(messages.size()));
}

} // namespace Backend
