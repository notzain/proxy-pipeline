#include "InProcRing.hpp"

namespace Proxy::Backend::InProc {

InProcRing::InProcRing() = default;

InProcRing::~InProcRing()
{
    stop();
}

bool InProcRing::push(std::span<const std::byte> message)
{
    return m_queue.pushBatch(1, [&](std::vector<std::byte>& slot, std::size_t) {
        slot.assign(message.begin(), message.end());
    });
}

std::size_t InProcRing::pushBatch(std::span<const std::span<const std::byte>> messages)
{
    if (messages.empty()) {
        return 0;
    }
    bool success = m_queue.pushBatch(messages.size(), [&](std::vector<std::byte>& slot, std::size_t idx) {
        const auto& msg = messages[idx];
        slot.assign(msg.begin(), msg.end());
    });
    return success ? messages.size() : 0;
}

void InProcRing::stop() noexcept
{
    m_queue.stop();
}

std::size_t InProcRing::size() const noexcept
{
    return m_queue.size();
}

bool InProcRing::empty() const noexcept
{
    return m_queue.empty();
}

} // namespace Proxy::Backend::InProc

