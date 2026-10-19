#pragma once

#include "backend/IOutputChannel.hpp"
#include <memory>
#include <mutex>
#include <string>

namespace Proxy::Backend::Shm {

class ShmRingBuffer;

/**
 * @brief Zero-copy output channel writing messages directly to a shared memory ring buffer.
 */
class ShmOutputChannel : public ::Backend::IOutputChannel {
public:
    /**
     * @brief Constructs a shared memory output channel.
     *
     * @param destPath Shared memory destination file path.
     * @param capacity Number of ring buffer slots.
     */
    explicit ShmOutputChannel(std::string destPath, std::size_t capacity = 1024);
    ~ShmOutputChannel() override;

    bool send(std::span<const std::byte> message) override;
    std::size_t sendBatch(std::span<const std::span<const std::byte>> messages) override;

    /**
     * @brief Checks if the underlying shared memory ring buffer is initialized.
     */
    [[nodiscard]] bool isValid() const noexcept;

private:
    std::string m_destPath;
    std::unique_ptr<ShmRingBuffer> m_ring;
    mutable std::mutex m_sendMutex;
};

} // namespace Proxy::Backend::Shm
