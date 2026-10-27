#pragma once

#include "backend/IBackend.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Core::Queue {
template <typename T, std::size_t Capacity>
    requires((Capacity & (Capacity - 1)) == 0 && Capacity >= 2)
class SpscQueue;
} // namespace Core::Queue

namespace Proxy::Backend::InProc {

class InProcRing;

/**
 * @brief High-performance in-process transport backend.
 *
 * Implements IBackend by dynamically pairing named input and output channels
 * to in-memory lock-free SPSC ring buffers with zero kernel crossings.
 */
class InProcBackend : public ::Backend::IBackend {
public:
    InProcBackend();
    ~InProcBackend() override;

    InProcBackend(const InProcBackend&) = delete;
    InProcBackend& operator=(const InProcBackend&) = delete;
    InProcBackend(InProcBackend&&) noexcept;
    InProcBackend& operator=(InProcBackend&&) noexcept;

    void configure(const ::Backend::IConfig& config) override;

    std::unique_ptr<::Backend::IInputChannel> createInputChannel(
        std::string_view name,
        const ::Backend::IConfig& config) override;

    std::unique_ptr<::Backend::IOutputChannel> createOutputChannel(
        std::string_view name,
        const ::Backend::IConfig& config) override;

    /**
     * @brief Gets or creates a named in-process message ring.
     *
     * @param name Logical name of the channel.
     * @return std::shared_ptr<InProcRing> Shared ring buffer handle.
     */
    std::shared_ptr<InProcRing> getOrCreateRing(std::string_view name);

    /**
     * @brief Clears all registered rings.
     */
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Proxy::Backend::InProc

