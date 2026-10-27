#pragma once

#include "InProcRing.hpp"
#include "backend/IInputChannel.hpp"
#include <memory>
#include <string>

namespace Proxy::Backend::InProc {

/**
 * @brief In-process message input channel reading from a lock-free SpscQueue ring.
 */
class InProcInputChannel : public ::Backend::IInputChannel {
public:
    InProcInputChannel(std::string name, std::shared_ptr<InProcRing> ring);
    ~InProcInputChannel() override;

    ::Backend::BytesResult receiveInto(std::span<std::byte> destination, Timeout timeout) override;

    ::Backend::BatchResult receiveBatch(
        std::span<::Backend::BufferSlot> slots,
        Timeout timeout) override;

private:
    std::shared_ptr<InProcRing> m_ring;
};

} // namespace Proxy::Backend::InProc

