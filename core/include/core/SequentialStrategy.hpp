#pragma once

#include "core/IExecutionStrategy.hpp"
#include <atomic>
#include <thread>

namespace Core {

/**
 * @brief Single-threaded round-robin execution strategy.
 *
 * Iterates through registered input channels sequentially in a worker thread,
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
    std::jthread m_workerThread;
};

} // namespace Core
