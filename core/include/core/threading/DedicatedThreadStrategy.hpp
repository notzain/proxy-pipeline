#pragma once

#include "common/compat/Jthread.hpp"
#include "core/threading/IExecutionStrategy.hpp"
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace Core::Threading {

/**
 * @brief Multi-threaded execution strategy assigning a dedicated polling thread to each input channel.
 *
 * Each channel thread continuously receives from its designated channel without blocking
 * other channels. If handler access serialization is disabled, callbacks for different channels
 * and step() may execute concurrently.
 */
class DedicatedThreadStrategy : public IExecutionStrategy {
public:
    explicit DedicatedThreadStrategy(bool serializeHandlerAccess = false);
    ~DedicatedThreadStrategy() override;

    void start(Handler::IHandler& handler) override;
    void stop() override;
    void wait() override;
    [[nodiscard]] bool isRunning() const noexcept override;

private:
    const bool m_serializeHandlerAccess;
    std::mutex m_handlerMutex;
    std::atomic<bool> m_running { false };
    std::vector<Common::Compat::jthread> m_workerThreads;
};

} // namespace Core::Threading
