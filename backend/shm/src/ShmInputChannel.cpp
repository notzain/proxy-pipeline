#include "backend/shm/ShmInputChannel.hpp"
#include "backend/shm/ShmRingBuffer.hpp"
#include <chrono>
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

ShmInputChannel::ShmInputChannel(std::string name, std::string path, std::size_t capacity)
    : ::Backend::IInputChannel(std::move(name))
    , m_path(std::move(path))
    , m_ring(std::make_unique<ShmRingBuffer>(m_path, capacity))
{
}

ShmInputChannel::~ShmInputChannel() = default;

bool ShmInputChannel::isValid() const noexcept
{
    return m_ring && m_ring->isValid();
}

void ShmInputChannel::reset()
{
    if (m_ring) {
        m_ring->reset();
    }
}

std::size_t ShmInputChannel::receiveInto(std::span<std::byte> destination, Timeout timeout)
{
    if (!isValid()) {
        return 0;
    }

    // Fast-path: non-blocking immediate dequeue
    bool wasFull = false;
    std::size_t bytes = m_ring->popInto(destination, &wasFull);
    if (wasFull && bytes > 0) {
        ShmSignaler::notifyOne(m_ring->notFullSync());
    }
    if (bytes > 0 || timeout.count() == 0) {
        return bytes;
    }

    auto start = std::chrono::steady_clock::now();
    auto deadline = start + timeout;

    // Adaptive spin for ultra-low latency before falling back to cross-process signaling wait
    for (int spin = 0; spin < 64; ++spin) {
        cpuPause();
        bytes = m_ring->popInto(destination, &wasFull);
        if (bytes > 0) {
            if (wasFull) {
                ShmSignaler::notifyOne(m_ring->notFullSync());
            }
            return bytes;
        }
    }

    // Kernel bypass event signaling wait (futex / pshared cond)
    while (std::chrono::steady_clock::now() < deadline) {
        auto remaining = std::chrono::duration_cast<Timeout>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            break;
        }
        auto waitDuration = std::min(remaining, Timeout(10));
        ShmSignaler::wait(m_ring->notEmptySync(), waitDuration);

        bytes = m_ring->popInto(destination, &wasFull);
        if (bytes > 0) {
            if (wasFull) {
                ShmSignaler::notifyOne(m_ring->notFullSync());
            }
            return bytes;
        }
    }

    return 0;
}

std::size_t ShmInputChannel::receiveBatch(
    std::span<const std::span<std::byte>> destinations,
    std::span<std::size_t> outSizes,
    Timeout timeout)
{
    if (!isValid() || destinations.empty() || outSizes.empty()) {
        return 0;
    }

    // Fast-path: speculative burst dequeue
    bool wasFull = false;
    std::size_t count = m_ring->popBatch(destinations, outSizes, &wasFull);
    if (wasFull && count > 0) {
        ShmSignaler::notifyOne(m_ring->notFullSync());
    }
    if (count > 0 || timeout.count() == 0) {
        return count;
    }

    auto start = std::chrono::steady_clock::now();
    auto deadline = start + timeout;

    // Adaptive spin for high-speed burst transitions
    for (int spin = 0; spin < 64; ++spin) {
        cpuPause();
        count = m_ring->popBatch(destinations, outSizes, &wasFull);
        if (count > 0) {
            if (wasFull) {
                ShmSignaler::notifyOne(m_ring->notFullSync());
            }
            return count;
        }
    }

    // Kernel bypass event signaling wait (futex / pshared cond)
    while (std::chrono::steady_clock::now() < deadline) {
        auto remaining = std::chrono::duration_cast<Timeout>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            break;
        }
        auto waitDuration = std::min(remaining, Timeout(10));
        ShmSignaler::wait(m_ring->notEmptySync(), waitDuration);

        count = m_ring->popBatch(destinations, outSizes, &wasFull);
        if (count > 0) {
            if (wasFull) {
                ShmSignaler::notifyOne(m_ring->notFullSync());
            }
            return count;
        }
    }

    return 0;
}

} // namespace Proxy::Backend::Shm
