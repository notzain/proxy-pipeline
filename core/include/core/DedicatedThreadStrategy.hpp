#pragma once

#include "core/IExecutionStrategy.hpp"
#include <atomic>
#include <thread>
#include <vector>

namespace Core {

/**
 * @brief Multi-threaded execution strategy assigning a dedicated polling thread to each input channel.
 *
 * Each channel thread continuously receives from its designated channel without blocking
 * other channels. If a handler implements step(), a separate worker thread or the main thread
 * periodically calls step().
 */
class DedicatedThreadStrategy : public IExecutionStrategy {
public:
    DedicatedThreadStrategy();
    ~DedicatedThreadStrategy() override;

    void start(IHandler& handler) override;
    void stop() override;
    void wait() override;
    [[nodiscard]] bool isRunning() const noexcept override;

private:
    std::atomic<bool> m_running { false };
    std::vector<std::jthread> m_workerThreads;
};

} // namespace Core
