#pragma once

#include "InProcRing.hpp"
#include "backend/channel/IInputChannel.hpp"
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

    Common::Net::BytesResult receiveSlot(::Backend::BufferSlot& slot, Timeout timeout) override;

    Common::Net::BatchResult receiveBatch(
        Common::Compat::span<::Backend::BufferSlot> slots,
        Timeout timeout) override;

private:
    std::shared_ptr<InProcRing> m_ring;
};

} // namespace Proxy::Backend::InProc
