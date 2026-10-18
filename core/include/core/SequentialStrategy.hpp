#pragma once

#include "core/IExecutionStrategy.hpp"
#include <atomic>

namespace Core {

/**
 * @brief Single-threaded round-robin execution strategy.
 *
 * Iterates through registered input channels sequentially in the calling thread,
 * polling each with a small timeout, dispatching messages, and invoking step().
 */
class SequentialStrategy : public IExecutionStrategy {
public:
    SequentialStrategy();
    ~SequentialStrategy() override;

    void start(IHandler& handler) override;
    void stop() override;
    void wait() override;
    [[nodiscard]] bool isRunning() const noexcept override;

private:
    std::atomic<bool> m_running { false };
};

} // namespace Core
