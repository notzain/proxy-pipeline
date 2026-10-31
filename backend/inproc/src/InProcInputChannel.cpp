#include "InProcInputChannel.hpp"
#include <algorithm>
#include <cstring>

namespace Proxy::Backend::InProc {

InProcInputChannel::InProcInputChannel(std::string name, std::shared_ptr<InProcRing> ring)
    : ::Backend::IInputChannel(std::move(name))
    , m_ring(std::move(ring))
{
}

InProcInputChannel::~InProcInputChannel() = default;

Common::Net::BytesResult InProcInputChannel::receiveSlot(::Backend::BufferSlot& slot, Timeout)
{
    slot.size = 0;
    if (!m_ring) {
        return Common::Net::BytesResult::err(Common::Net::ReceiveClosed { });
    }

    std::size_t copiedBytes = 0;
    bool found = false;

    std::size_t drained = m_ring->drain(1, [&](Common::Compat::span<const std::vector<std::byte>> slice) {
        if (!slice.empty()) {
            const auto& msg = slice[0];
            copiedBytes = std::min(slot.buffer.size(), msg.size());
            std::memcpy(slot.buffer.data(), msg.data(), copiedBytes);
            found = true;
        }
    });

    if (drained > 0 && found) {
        slot.size = copiedBytes;
        return Common::Net::BytesResult::ok(copiedBytes);
    }

    return Common::Net::BytesResult::err(Common::Net::ReceiveTimeout { });
}

Common::Net::BatchResult InProcInputChannel::receiveBatch(
    Common::Compat::span<::Backend::BufferSlot> slots,
    Timeout)
{
    if (!m_ring) {
        return Common::Net::BatchResult::err({ 0, Common::Net::ReceiveClosed { } });
    }

    if (slots.empty()) {
        return Common::Net::BatchResult::ok(0);
    }

    std::size_t count = 0;
    m_ring->drain(slots.size(), [&](Common::Compat::span<const std::vector<std::byte>> slice) {
        for (std::size_t i = 0; i < slice.size() && i < slots.size(); ++i) {
            const auto& msg = slice[i];
            const std::size_t toCopy = std::min(slots[i].buffer.size(), msg.size());
            std::memcpy(slots[i].buffer.data(), msg.data(), toCopy);
            slots[i].size = toCopy;
            ++count;
        }
    });

    if (count > 0) {
        return Common::Net::BatchResult::ok(count);
    }

    return Common::Net::BatchResult::err({ 0, Common::Net::ReceiveTimeout { } });
}

} // namespace Proxy::Backend::InProc
