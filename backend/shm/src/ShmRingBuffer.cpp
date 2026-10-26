#include "backend/shm/ShmRingBuffer.hpp"
#include "log/Log.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Proxy::Backend::Shm {

ShmRingBuffer::ShmRingBuffer(std::string path, std::size_t capacity)
    : m_path(std::move(path))
{
    // Ensure capacity is a power of two, defaulting to 1024 if not
    if (capacity == 0 || (capacity & (capacity - 1)) != 0) {
        capacity = DefaultCapacity;
    }
    mapMemory(capacity);
}

ShmRingBuffer::~ShmRingBuffer()
{
    unmapMemory();
}

ShmRingBuffer::ShmRingBuffer(ShmRingBuffer&& other) noexcept
    : m_path(std::move(other.m_path))
    , m_fd(other.m_fd)
    , m_mappedMemory(other.m_mappedMemory)
    , m_mappedSize(other.m_mappedSize)
    , m_header(other.m_header)
    , m_slots(other.m_slots)
{
    other.m_fd = -1;
    other.m_mappedMemory = nullptr;
    other.m_mappedSize = 0;
    other.m_header = nullptr;
    other.m_slots = nullptr;
}

ShmRingBuffer& ShmRingBuffer::operator=(ShmRingBuffer&& other) noexcept
{
    if (this != &other) {
        unmapMemory();
        m_path = std::move(other.m_path);
        m_fd = other.m_fd;
        m_mappedMemory = other.m_mappedMemory;
        m_mappedSize = other.m_mappedSize;
        m_header = other.m_header;
        m_slots = other.m_slots;

        other.m_fd = -1;
        other.m_mappedMemory = nullptr;
        other.m_mappedSize = 0;
        other.m_header = nullptr;
        other.m_slots = nullptr;
    }
    return *this;
}

bool ShmRingBuffer::isValid() const noexcept
{
    return m_mappedMemory != nullptr && m_header != nullptr && m_slots != nullptr;
}

void ShmRingBuffer::mapMemory(std::size_t capacity)
{
    std::size_t totalBytes = sizeof(ShmHeader) + (capacity * sizeof(ShmSlot));
    // Align total mapped size to 4KB page boundary
    m_mappedSize = (totalBytes + 4095) & ~static_cast<std::size_t>(4095);

    m_fd = ::open(m_path.c_str(), O_RDWR | O_CREAT, 0666);
    if (m_fd < 0) {
        Proxy::Log::error("[ShmRingBuffer] Failed to open shared memory file '{}': {}", m_path, std::strerror(errno));
        return;
    }

    struct stat st { };
    if (::fstat(m_fd, &st) < 0) {
        Proxy::Log::error("[ShmRingBuffer] Failed to fstat shared memory file '{}': {}", m_path, std::strerror(errno));
        ::close(m_fd);
        m_fd = -1;
        return;
    }

    bool isNewlyCreated = (st.st_size < static_cast<off_t>(m_mappedSize));
    if (isNewlyCreated) {
        if (::ftruncate(m_fd, static_cast<off_t>(m_mappedSize)) < 0) {
            Proxy::Log::error("[ShmRingBuffer] Failed to ftruncate file '{}' to size {}: {}", m_path, m_mappedSize, std::strerror(errno));
            ::close(m_fd);
            m_fd = -1;
            return;
        }
    }

    m_mappedMemory = ::mmap(nullptr, m_mappedSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
    if (m_mappedMemory == MAP_FAILED) {
        Proxy::Log::error("[ShmRingBuffer] Failed to mmap shared memory file '{}': {}", m_path, std::strerror(errno));
        m_mappedMemory = nullptr;
        ::close(m_fd);
        m_fd = -1;
        return;
    }

    m_header = static_cast<ShmHeader*>(m_mappedMemory);
    m_slots = reinterpret_cast<ShmSlot*>(static_cast<char*>(m_mappedMemory) + sizeof(ShmHeader));

    if (isNewlyCreated || m_header->magic != ShmHeader::Magic || m_header->version != ShmHeader::Version) {
        m_header->magic = ShmHeader::Magic;
        m_header->version = ShmHeader::Version;
        m_header->capacity = static_cast<uint32_t>(capacity);
        m_header->slotPayloadSize = static_cast<uint32_t>(MaxPayloadBytes);
        m_header->writeIndex.store(0, std::memory_order_relaxed);
        m_header->readIndex.store(0, std::memory_order_relaxed);

        ShmSignaler::init(&m_header->notEmptySync);
        ShmSignaler::init(&m_header->notFullSync);
    }
}

void ShmRingBuffer::unmapMemory()
{
    if (m_mappedMemory != nullptr && m_mappedMemory != MAP_FAILED) {
        ::munmap(m_mappedMemory, m_mappedSize);
        m_mappedMemory = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_header = nullptr;
    m_slots = nullptr;
}

bool ShmRingBuffer::push(std::span<const std::byte> message, bool* wasEmpty)
{
    if (!isValid() || message.size() > MaxPayloadBytes) {
        return false;
    }

    uint64_t w = m_header->writeIndex.load(std::memory_order_relaxed);
    uint64_t r = m_header->readIndex.load(std::memory_order_acquire);

    if (w - r >= m_header->capacity) {
        return false;
    }

    if (wasEmpty) {
        *wasEmpty = (w == r);
    }

    uint32_t slotIdx = static_cast<uint32_t>(w & (m_header->capacity - 1));
    ShmSlot& slot = m_slots[slotIdx];

    slot.length = static_cast<uint32_t>(message.size());
    std::memcpy(slot.payload, message.data(), message.size());

    m_header->writeIndex.store(w + 1, std::memory_order_release);
    return true;
}

std::size_t ShmRingBuffer::pushBatch(std::span<const std::span<const std::byte>> messages, bool* wasEmpty)
{
    if (!isValid() || messages.empty()) {
        return 0;
    }

    uint64_t w = m_header->writeIndex.load(std::memory_order_relaxed);
    uint64_t r = m_header->readIndex.load(std::memory_order_acquire);

    uint64_t occupied = (w >= r) ? (w - r) : 0;
    uint64_t available = (m_header->capacity > occupied) ? (m_header->capacity - occupied) : 0;
    std::size_t toWrite = std::min<std::size_t>(messages.size(), available);

    if (toWrite == 0) {
        return 0;
    }

    if (wasEmpty) {
        *wasEmpty = (w == r);
    }

    uint32_t mask = m_header->capacity - 1;
    for (std::size_t i = 0; i < toWrite; ++i) {
        const auto& msg = messages[i];
        if (msg.size() > MaxPayloadBytes) {
            toWrite = i;
            break;
        }
        uint32_t slotIdx = static_cast<uint32_t>((w + i) & mask);
        ShmSlot& slot = m_slots[slotIdx];
        slot.length = static_cast<uint32_t>(msg.size());
        std::memcpy(slot.payload, msg.data(), msg.size());
    }

    if (toWrite > 0) {
        m_header->writeIndex.store(w + toWrite, std::memory_order_release);
    }
    return toWrite;
}

std::size_t ShmRingBuffer::popInto(std::span<std::byte> destination, bool* wasFull)
{
    if (!isValid()) {
        return 0;
    }

    uint64_t r = m_header->readIndex.load(std::memory_order_relaxed);
    uint64_t w = m_header->writeIndex.load(std::memory_order_acquire);

    if (r >= w) {
        return 0;
    }

    if (wasFull) {
        *wasFull = (w - r >= m_header->capacity);
    }

    uint32_t slotIdx = static_cast<uint32_t>(r & (m_header->capacity - 1));
    const ShmSlot& slot = m_slots[slotIdx];

    std::size_t bytesToCopy = std::min<std::size_t>(slot.length, destination.size());
    std::memcpy(destination.data(), slot.payload, bytesToCopy);

    m_header->readIndex.store(r + 1, std::memory_order_release);
    return bytesToCopy;
}

std::size_t ShmRingBuffer::popBatch(
    std::span<::Backend::BufferSlot> slots,
    bool* wasFull)
{
    if (!isValid() || slots.empty()) {
        return 0;
    }

    uint64_t r = m_header->readIndex.load(std::memory_order_relaxed);
    uint64_t w = m_header->writeIndex.load(std::memory_order_acquire);

    if (r >= w) {
        return 0;
    }

    if (wasFull) {
        *wasFull = (w - r >= m_header->capacity);
    }

    uint64_t ready = w - r;
    std::size_t toRead = std::min(slots.size(), static_cast<std::size_t>(ready));

    uint32_t mask = m_header->capacity - 1;
    for (std::size_t i = 0; i < toRead; ++i) {
        uint32_t slotIdx = static_cast<uint32_t>((r + i) & mask);
        const ShmSlot& slot = m_slots[slotIdx];
        std::size_t bytesToCopy = std::min<std::size_t>(slot.length, slots[i].buffer.size());
        std::memcpy(slots[i].buffer.data(), slot.payload, bytesToCopy);
        slots[i].size = bytesToCopy;
    }

    m_header->readIndex.store(r + toRead, std::memory_order_release);
    return toRead;
}

std::size_t ShmRingBuffer::size() const noexcept
{
    if (!isValid()) {
        return 0;
    }
    uint64_t w = m_header->writeIndex.load(std::memory_order_relaxed);
    uint64_t r = m_header->readIndex.load(std::memory_order_relaxed);
    return (w >= r) ? static_cast<std::size_t>(w - r) : 0;
}

bool ShmRingBuffer::empty() const noexcept
{
    return size() == 0;
}

bool ShmRingBuffer::full() const noexcept
{
    if (!isValid()) {
        return true;
    }
    return size() >= m_header->capacity;
}

void ShmRingBuffer::reset()
{
    if (isValid()) {
        m_header->writeIndex.store(0, std::memory_order_release);
        m_header->readIndex.store(0, std::memory_order_release);
    }
}

const std::string& ShmRingBuffer::getPath() const noexcept
{
    return m_path;
}

} // namespace Proxy::Backend::Shm
