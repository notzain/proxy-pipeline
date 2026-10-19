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

ShmInputChannel::ShmInputChannel(std::string path, std::size_t capacity)
    : m_path(std::move(path))
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

std::optional<::Backend::IInputChannel::Buffer> ShmInputChannel::receive(Timeout timeout)
{
    std::array<std::byte, ShmRingBuffer::MaxPayloadBytes> tempBuffer;
    std::size_t bytes = receiveInto(tempBuffer, timeout);
    if (bytes == 0) {
        return std::nullopt;
    }
    return Buffer(tempBuffer.begin(), tempBuffer.begin() + bytes);
}

std::size_t ShmInputChannel::receiveInto(std::span<std::byte> destination, Timeout timeout)
{
    if (!isValid()) {
        return 0;
    }

    // Fast-path: non-blocking immediate dequeue
    std::size_t bytes = m_ring->popInto(destination);
    if (bytes > 0 || timeout.count() == 0) {
        return bytes;
    }

    auto start = std::chrono::steady_clock::now();
    auto deadline = start + timeout;

    // Adaptive spin for ultra-low latency before falling back to thread sleep
    for (int spin = 0; spin < 64; ++spin) {
        cpuPause();
        bytes = m_ring->popInto(destination);
        if (bytes > 0) {
            return bytes;
        }
    }

    // Backoff sleep until message arrives or timeout expires
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        bytes = m_ring->popInto(destination);
        if (bytes > 0) {
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
    std::size_t count = m_ring->popBatch(destinations, outSizes);
    if (count > 0 || timeout.count() == 0) {
        return count;
    }

    auto start = std::chrono::steady_clock::now();
    auto deadline = start + timeout;

    // Adaptive spin for high-speed burst transitions
    for (int spin = 0; spin < 64; ++spin) {
        cpuPause();
        count = m_ring->popBatch(destinations, outSizes);
        if (count > 0) {
            return count;
        }
    }

    // Sleep backoff until batch arrives or timeout expires
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        count = m_ring->popBatch(destinations, outSizes);
        if (count > 0) {
            return count;
        }
    }

    return 0;
}

} // namespace Proxy::Backend::Shm
