#pragma once

#include "common/compat/Span.hpp"
#include "common/queue/SpscQueue.hpp"
#include <cstddef>
#include <memory>
#include <vector>

namespace Proxy::Backend::InProc {

/**
 * @brief Lock-free in-memory message queue supporting batch pushing and draining.
 */
class InProcRing {
public:
    static constexpr std::size_t DefaultCapacity = 16384;

    using QueueType = Common::Queue::SpscQueue<std::vector<std::byte>, DefaultCapacity>;

    InProcRing();
    ~InProcRing();

    bool push(Common::Compat::span<const std::byte> message);
    std::size_t pushBatch(Common::Compat::span<const Common::Compat::span<const std::byte>> messages);

    template <typename ConsumerFn>
    std::size_t drain(std::size_t maxCount, ConsumerFn&& consumer)
    {
        return m_queue.tryDrainContiguous(maxCount, std::forward<ConsumerFn>(consumer));
    }

    void stop() noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

private:
    QueueType m_queue;
};

} // namespace Proxy::Backend::InProc
