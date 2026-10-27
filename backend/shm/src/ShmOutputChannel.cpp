#include "backend/shm/ShmOutputChannel.hpp"
#include "backend/shm/ShmRingBuffer.hpp"
#include <thread>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace Proxy::Backend::Shm {

namespace {

    inline void cpuPause() noexcept
    {
#if defined(__x86_64__) || defined(_M_X64)
        _mm_pause();
#elif defined(__aarch64__)
        asm volatile("yield");
#else
        std::this_thread::yield();
#endif
    }

} // namespace

ShmOutputChannel::ShmOutputChannel(std::string name, std::string destPath, std::size_t capacity)
    : ::Backend::IOutputChannel(std::move(name))
    , m_destPath(std::move(destPath))
    , m_ring(std::make_unique<ShmRingBuffer>(m_destPath, capacity))
{
}

ShmOutputChannel::~ShmOutputChannel() = default;

bool ShmOutputChannel::isValid() const noexcept
{
    return m_ring && m_ring->isValid();
}

bool ShmOutputChannel::send(Common::Compat::span<const std::byte> message)
{
    if (!isValid() || message.size() > ShmRingBuffer::MaxPayloadBytes) {
        return false;
    }

    std::lock_guard lock(m_sendMutex);

    bool wasEmpty = false;
    if (m_ring->push(message, &wasEmpty)) {
        if (wasEmpty) {
            ShmSignaler::notifyOne(m_ring->notEmptySync());
        }
        return true;
    }

    // Adaptive backpressure: spin, then yield, then wait on notFullSync
    auto start = std::chrono::steady_clock::now();
    constexpr auto timeout = std::chrono::seconds(2);

    for (int retry = 0; (std::chrono::steady_clock::now() - start) < timeout; ++retry) {
        if (retry < 64) {
            cpuPause();
        } else if (retry < 256) {
            std::this_thread::yield();
        } else {
            ShmSignaler::wait(m_ring->notFullSync(), std::chrono::milliseconds(2));
        }

        if (m_ring->push(message, &wasEmpty)) {
            if (wasEmpty) {
                ShmSignaler::notifyOne(m_ring->notEmptySync());
            }
            return true;
        }
    }

    return false;
}

std::size_t ShmOutputChannel::sendBatch(Common::Compat::span<const Common::Compat::span<const std::byte>> messages)
{
    if (!isValid() || messages.empty()) {
        return 0;
    }

    std::lock_guard lock(m_sendMutex);

    bool wasEmpty = false;
    std::size_t total = m_ring->pushBatch(messages, &wasEmpty);
    if (wasEmpty && total > 0) {
        ShmSignaler::notifyOne(m_ring->notEmptySync());
    }
    if (total == messages.size()) {
        return total;
    }

    auto start = std::chrono::steady_clock::now();
    constexpr auto timeout = std::chrono::seconds(2);

    // If partial write due to ring capacity, retry with adaptive backpressure until deadline
    for (int retry = 0; total < messages.size() && (std::chrono::steady_clock::now() - start) < timeout; ++retry) {
        if (retry < 64) {
            cpuPause();
        } else if (retry < 256) {
            std::this_thread::yield();
        } else {
            ShmSignaler::wait(m_ring->notFullSync(), std::chrono::milliseconds(2));
        }
        bool chunkWasEmpty = false;
        std::size_t chunk = m_ring->pushBatch(messages.subspan(total), &chunkWasEmpty);
        if (chunkWasEmpty && chunk > 0) {
            ShmSignaler::notifyOne(m_ring->notEmptySync());
        }
        total += chunk;
    }

    return total;
}

} // namespace Proxy::Backend::Shm
