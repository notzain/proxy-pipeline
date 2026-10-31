#include "backend/IOutputChannel.hpp"
#include "backend/EnvelopeHeader.hpp"
#include "backend/MessageView.hpp"
#include "common/memory/SmallSpanBuffer.hpp"
#include <array>
#include <utility>
#include <vector>

namespace Backend {

namespace {

    bool sendViewAsEnvelope(IOutputChannel& channel, const MessageView& message)
    {
        if (message.hasWireEnvelope()) {
            return channel.send(message.rawBuffer());
        }

        const std::size_t totalSize = sizeof(EnvelopeHeader) + message.metadata().size() + message.payload().size();
        constexpr std::size_t StackBufferBytes = 4096;
        if (totalSize <= StackBufferBytes) {
            std::array<std::byte, StackBufferBytes> stackBuf;
            const std::size_t written = MessageView::packInto(
                Common::Compat::span<std::byte>(stackBuf.data(), totalSize),
                message.protocol(),
                message.metadata(),
                message.payload(),
                message.timestampNs());
            return written > 0 && channel.send(Common::Compat::span<const std::byte>(stackBuf.data(), written));
        }

        const auto packed = MessageView::pack(
            message.protocol(),
            message.metadata(),
            message.payload(),
            message.timestampNs());
        return channel.send(Common::Compat::span<const std::byte>(packed.data(), packed.size()));
    }

} // namespace

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
    return sendViewAsEnvelope(*this, message);
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

    bool allHaveWireEnvelope = true;
    for (const auto& msg : messages) {
        if (!msg.hasWireEnvelope()) {
            allHaveWireEnvelope = false;
            break;
        }
    }

    if (allHaveWireEnvelope) {
        Common::Memory::SmallSpanBuffer spans(messages.size());
        for (std::size_t i = 0; i < messages.size(); ++i) {
            spans[i] = messages[i].rawBuffer();
        }
        return sendBatch(spans.span(messages.size()));
    }

    std::size_t count = 0;
    for (const auto& msg : messages) {
        if (send(msg)) {
            ++count;
        }
    }
    return count;
}

} // namespace Backend
