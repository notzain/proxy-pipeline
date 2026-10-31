#pragma once

#include "InProcRing.hpp"
#include "backend/channel/IOutputChannel.hpp"
#include <memory>
#include <string>

namespace Proxy::Backend::InProc {

/**
 * @brief In-process message output channel publishing to a lock-free SpscQueue ring.
 */
class InProcOutputChannel : public ::Backend::IOutputChannel {
public:
    InProcOutputChannel(std::string name, std::shared_ptr<InProcRing> ring);
    ~InProcOutputChannel() override;

    using ::Backend::IOutputChannel::send;
    using ::Backend::IOutputChannel::sendBatch;

    bool send(Common::Compat::span<const std::byte> message) override;
    std::size_t sendBatch(Common::Compat::span<const Common::Compat::span<const std::byte>> messages) override;

private:
    std::shared_ptr<InProcRing> m_ring;
};

} // namespace Proxy::Backend::InProc
