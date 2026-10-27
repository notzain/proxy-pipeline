#pragma once

#include "core/queue/SpscQueue.hpp"
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace Proxy::Backend::InProc {

/**
 * @brief Lock-free in-memory message queue supporting batch pushing and draining.
 */
class InProcRing {
public:
    static constexpr std::size_t DefaultCapacity = 16384;

    using QueueType = Core::Queue::SpscQueue<std::vector<std::byte>, DefaultCapacity>;

    InProcRing();
    ~InProcRing();

    bool push(std::span<const std::byte> message);
    std::size_t pushBatch(std::span<const std::span<const std::byte>> messages);

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
