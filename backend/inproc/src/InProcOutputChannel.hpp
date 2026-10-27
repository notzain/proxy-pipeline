#pragma once

#include "InProcRing.hpp"
#include "backend/IOutputChannel.hpp"
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

    bool send(std::span<const std::byte> message) override;
    std::size_t sendBatch(std::span<const std::span<const std::byte>> messages) override;

private:
    std::shared_ptr<InProcRing> m_ring;
};

} // namespace Proxy::Backend::InProc

