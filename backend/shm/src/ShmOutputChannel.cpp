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

ShmOutputChannel::ShmOutputChannel(std::string destPath, std::size_t capacity)
    : m_destPath(std::move(destPath))
    , m_ring(std::make_unique<ShmRingBuffer>(m_destPath, capacity))
{
}

ShmOutputChannel::~ShmOutputChannel() = default;

bool ShmOutputChannel::isValid() const noexcept
{
    return m_ring && m_ring->isValid();
}

bool ShmOutputChannel::send(std::span<const std::byte> message)
{
    if (!isValid() || message.size() > ShmRingBuffer::MaxPayloadBytes) {
        return false;
    }

    std::lock_guard lock(m_sendMutex);

    if (m_ring->push(message)) {
        return true;
    }

    // Adaptive backpressure: spin, then yield, then sleep with deadline
    auto start = std::chrono::steady_clock::now();
    constexpr auto timeout = std::chrono::seconds(2);

    for (int retry = 0; (std::chrono::steady_clock::now() - start) < timeout; ++retry) {
        if (retry < 64) {
            cpuPause();
        } else if (retry < 256) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }

        if (m_ring->push(message)) {
            return true;
        }
    }

    return false;
}

std::size_t ShmOutputChannel::sendBatch(std::span<const std::span<const std::byte>> messages)
{
    if (!isValid() || messages.empty()) {
        return 0;
    }

    std::lock_guard lock(m_sendMutex);

    std::size_t total = m_ring->pushBatch(messages);
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
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
        std::size_t chunk = m_ring->pushBatch(messages.subspan(total));
        total += chunk;
    }

    return total;
}

} // namespace Proxy::Backend::Shm
