#pragma once

#include "core/threading/IExecutionStrategy.hpp"
#include <atomic>
#include <thread>

namespace Core::Threading {

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

    void start(Handler::IHandler& handler) override;
    void stop() override;
    void wait() override;
    [[nodiscard]] bool isRunning() const noexcept override;

private:
    std::atomic<bool> m_running { false };
    std::jthread m_workerThread;
};

} // namespace Core::Threading

namespace Core {
using Threading::SequentialStrategy;
} // namespace Core
