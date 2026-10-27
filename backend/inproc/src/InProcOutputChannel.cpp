#include "InProcOutputChannel.hpp"

namespace Proxy::Backend::InProc {

InProcOutputChannel::InProcOutputChannel(std::string name, std::shared_ptr<InProcRing> ring)
    : ::Backend::IOutputChannel(std::move(name))
    , m_ring(std::move(ring))
{
}

InProcOutputChannel::~InProcOutputChannel() = default;

bool InProcOutputChannel::send(Common::Compat::span<const std::byte> message)
{
    if (!m_ring) {
        return false;
    }
    return m_ring->push(message);
}

std::size_t InProcOutputChannel::sendBatch(Common::Compat::span<const Common::Compat::span<const std::byte>> messages)
{
    if (!m_ring || messages.empty()) {
        return 0;
    }
    return m_ring->pushBatch(messages);
}

} // namespace Proxy::Backend::InProc
