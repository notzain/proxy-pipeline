#pragma once

#include "backend/IInputChannel.hpp"
#include <memory>
#include <string>

namespace Proxy::Backend::Shm {

using ::Backend::BatchResult;
using ::Backend::BytesResult;

class ShmRingBuffer;

/**
 * @brief Zero-copy input channel reading messages directly from a shared memory ring buffer.
 */
class ShmInputChannel : public ::Backend::IInputChannel {
public:
    /**
     * @brief Constructs a shared memory input channel.
     *
     * @param name Logical channel identifier.
     * @param path Shared memory file path.
     * @param capacity Number of ring buffer slots.
     */
    ShmInputChannel(std::string name, std::string path, std::size_t capacity = 1024);
    ~ShmInputChannel() override;

    BytesResult receiveInto(Common::Compat::span<std::byte> destination, Timeout timeout) override;
    BatchResult receiveBatch(
        Common::Compat::span<::Backend::BufferSlot> slots,
        Timeout timeout) override;

    /**
     * @brief Checks if the underlying shared memory ring buffer is initialized.
     */
    [[nodiscard]] bool isValid() const noexcept;

    /**
     * @brief Resets the underlying shared memory indices.
     */
    void reset();

private:
    std::string m_path;
    std::unique_ptr<ShmRingBuffer> m_ring;
};

} // namespace Proxy::Backend::Shm
