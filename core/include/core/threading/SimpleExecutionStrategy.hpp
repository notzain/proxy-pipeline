#pragma once

#include "common/compat/Jthread.hpp"
#include "core/threading/IExecutionStrategy.hpp"
#include <atomic>

namespace Core::Threading {

/**
 * @brief Minimal, single-threaded execution strategy designed for maximum traceability and debugging.
 *
 * Runs a deterministic, synchronous polling loop across all registered input channels.
 * Directly dispatches received messages into handler callbacks without worker thread pools,
 * queues, or concurrent task scheduling.
 */
class SimpleExecutionStrategy : public IExecutionStrategy {
public:
    SimpleExecutionStrategy() = default;
    ~SimpleExecutionStrategy() override;

    void start(Handler::IHandler& handler) override;
    void stop() override;
    void wait() override;
    [[nodiscard]] bool isRunning() const noexcept override;

private:
    std::atomic<bool> m_running { false };
    Common::Compat::jthread m_workerThread;
};

} // namespace Core::Threading

