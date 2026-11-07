#include "backend/shm/ShmRingBuffer.hpp"
#include "log/Log.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Proxy::Backend::Shm {

ShmRingBuffer::ShmRingBuffer(std::string path, std::size_t capacity)
    : m_path(std::move(path))
{
    if (capacity == 0 || (capacity & (capacity - 1)) != 0
        || capacity > std::numeric_limits<uint32_t>::max()) {
        Proxy::Log::error("[ShmRingBuffer] Invalid capacity {} for '{}'", capacity, m_path);
        return;
    }
    m_capacity = capacity;
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
    , m_capacity(other.m_capacity)
    , m_corrupted(other.m_corrupted.load(std::memory_order_relaxed))
    , m_invalidSlotCount(other.m_invalidSlotCount.load(std::memory_order_relaxed))
{
    other.m_fd = -1;
    other.m_mappedMemory = nullptr;
    other.m_mappedSize = 0;
    other.m_header = nullptr;
    other.m_slots = nullptr;
    other.m_capacity = 0;
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
        m_capacity = other.m_capacity;
        m_corrupted.store(other.m_corrupted.load(std::memory_order_relaxed), std::memory_order_relaxed);
        m_invalidSlotCount.store(other.m_invalidSlotCount.load(std::memory_order_relaxed), std::memory_order_relaxed);

        other.m_fd = -1;
        other.m_mappedMemory = nullptr;
        other.m_mappedSize = 0;
        other.m_header = nullptr;
        other.m_slots = nullptr;
        other.m_capacity = 0;
    }
    return *this;
}

bool ShmRingBuffer::isValid() const noexcept
{
    return m_mappedMemory != nullptr && m_header != nullptr && m_slots != nullptr
        && m_capacity != 0 && !m_corrupted.load(std::memory_order_relaxed);
}

void ShmRingBuffer::mapMemory(std::size_t capacity)
{
    unmapMemory();

    if (capacity > (std::numeric_limits<std::size_t>::max() - sizeof(ShmHeader)) / sizeof(ShmSlot)) {
        Proxy::Log::error("[ShmRingBuffer] Capacity {} overflows mapping size for '{}'", capacity, m_path);
        return;
    }

    const std::size_t totalBytes = sizeof(ShmHeader) + capacity * sizeof(ShmSlot);
    if (totalBytes > static_cast<std::size_t>(std::numeric_limits<off_t>::max())) {
        Proxy::Log::error("[ShmRingBuffer] Mapping size {} is unsupported for '{}'", totalBytes, m_path);
        return;
    }

    int openFlags = O_RDWR;
#ifdef O_CLOEXEC
    openFlags |= O_CLOEXEC;
#endif
#ifdef O_NOFOLLOW
    openFlags |= O_NOFOLLOW;
#endif

    bool isNewlyCreated = false;
    m_fd = ::open(m_path.c_str(), openFlags | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (m_fd >= 0) {
        isNewlyCreated = true;
    } else if (errno == EEXIST) {
        m_fd = ::open(m_path.c_str(), openFlags);
    }
    if (m_fd < 0) {
        Proxy::Log::error("[ShmRingBuffer] Failed to open shared memory file '{}': {}", m_path, std::strerror(errno));
        return;
    }

    if (::flock(m_fd, LOCK_EX) != 0) {
        Proxy::Log::error("[ShmRingBuffer] Failed to lock shared memory file '{}': {}", m_path, std::strerror(errno));
        unmapMemory();
        return;
    }

    struct FileLock {
        explicit FileLock(int descriptor)
            : fd(descriptor)
        {
        }
        ~FileLock() { unlock(); }
        void unlock()
        {
            if (fd >= 0) {
                ::flock(fd, LOCK_UN);
                fd = -1;
            }
        }
        int fd;
    } fileLock { m_fd };

    struct stat st { };
    if (::fstat(m_fd, &st) != 0) {
        Proxy::Log::error("[ShmRingBuffer] Failed to inspect shared memory file '{}': {}", m_path, std::strerror(errno));
        fileLock.unlock();
        unmapMemory();
        return;
    }
    if (!S_ISREG(st.st_mode) || st.st_uid != ::geteuid()) {
        Proxy::Log::error("[ShmRingBuffer] Shared memory path '{}' is not a regular file owned by this user", m_path);
        fileLock.unlock();
        unmapMemory();
        return;
    }
    if (::fchmod(m_fd, S_IRUSR | S_IWUSR) != 0) {
        Proxy::Log::error("[ShmRingBuffer] Failed to restrict permissions on '{}': {}", m_path, std::strerror(errno));
        fileLock.unlock();
        unmapMemory();
        return;
    }

    if (isNewlyCreated) {
        if (::ftruncate(m_fd, static_cast<off_t>(totalBytes)) != 0) {
            Proxy::Log::error("[ShmRingBuffer] Failed to truncate shared memory file '{}' to {} bytes: {}",
                m_path, totalBytes, std::strerror(errno));
            fileLock.unlock();
            unmapMemory();
            return;
        }
    } else if (st.st_size < 0 || static_cast<std::uintmax_t>(st.st_size) != totalBytes) {
        Proxy::Log::error("[ShmRingBuffer] Shared memory file '{}' has an incompatible size", m_path);
        fileLock.unlock();
        unmapMemory();
        return;
    }

    m_mappedMemory = ::mmap(nullptr, totalBytes, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
    if (m_mappedMemory == MAP_FAILED) {
        Proxy::Log::error("[ShmRingBuffer] Failed to mmap shared memory file '{}': {}", m_path, std::strerror(errno));
        m_mappedMemory = nullptr;
        fileLock.unlock();
        unmapMemory();
        return;
    }

    m_mappedSize = totalBytes;
    m_header = static_cast<ShmHeader*>(m_mappedMemory);
    m_slots = reinterpret_cast<ShmSlot*>(static_cast<char*>(m_mappedMemory) + sizeof(ShmHeader));

    if (isNewlyCreated) {
        m_header->magic = ShmHeader::Magic;
        m_header->version = ShmHeader::Version;
        m_header->capacity = static_cast<uint32_t>(capacity);
        m_header->slotPayloadSize = static_cast<uint32_t>(MaxPayloadBytes);
        m_header->writeIndex.store(0, std::memory_order_relaxed);
        m_header->readIndex.store(0, std::memory_order_relaxed);

        for (uint32_t i = 0; i < capacity; ++i) {
            m_slots[i].sequence.store(i, std::memory_order_relaxed);
            m_slots[i].length = 0;
        }

        ShmSignaler::init(&m_header->notEmptySync);
        ShmSignaler::init(&m_header->notFullSync);
    } else if (m_header->magic != ShmHeader::Magic
        || m_header->version != ShmHeader::Version
        || m_header->capacity != capacity
        || m_header->capacity == 0
        || (m_header->capacity & (m_header->capacity - 1)) != 0
        || m_header->slotPayloadSize != MaxPayloadBytes) {
        Proxy::Log::error("[ShmRingBuffer] Shared memory file '{}' has an incompatible header", m_path);
        fileLock.unlock();
        unmapMemory();
    }
}

void ShmRingBuffer::unmapMemory()
{
    if (m_mappedMemory != nullptr && m_mappedMemory != MAP_FAILED) {
        ::munmap(m_mappedMemory, m_mappedSize);
        m_mappedMemory = nullptr;
    }
    m_mappedSize = 0;
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_header = nullptr;
    m_slots = nullptr;
}

bool ShmRingBuffer::push(Common::Compat::span<const std::byte> message, bool* wasEmpty)
{
    if (!isValid() || message.size() > MaxPayloadBytes) {
        return false;
    }

    const uint64_t mask = m_capacity - 1;
    uint64_t pos = m_header->writeIndex.load(std::memory_order_relaxed);

    while (true) {
        ShmSlot& slot = m_slots[pos & mask];
        const uint64_t seq = slot.sequence.load(std::memory_order_acquire);
        const int64_t diff = static_cast<int64_t>(seq) - static_cast<int64_t>(pos);

        if (diff == 0) {
            if (m_header->writeIndex.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                if (wasEmpty) {
                    const uint64_t r = m_header->readIndex.load(std::memory_order_relaxed);
                    *wasEmpty = (pos == r);
                }
                slot.length = static_cast<uint32_t>(message.size());
                std::memcpy(slot.payload, message.data(), message.size());
                slot.sequence.store(pos + 1, std::memory_order_release);
                return true;
            }
        } else if (diff < 0) {
            return false;
        } else {
            pos = m_header->writeIndex.load(std::memory_order_relaxed);
        }
    }
}

std::size_t ShmRingBuffer::pushBatch(Common::Compat::span<const Common::Compat::span<const std::byte>> messages, bool* wasEmpty)
{
    if (!isValid() || messages.empty()) {
        return 0;
    }

    std::size_t count = 0;
    for (const auto& msg : messages) {
        bool slotWasEmpty = false;
        if (!push(msg, &slotWasEmpty)) {
            break;
        }
        if (count == 0 && wasEmpty) {
            *wasEmpty = slotWasEmpty;
        }
        ++count;
    }
    return count;
}

std::size_t ShmRingBuffer::popInto(Common::Compat::span<std::byte> destination, bool* wasFull)
{
    if (!isValid()) {
        return 0;
    }

    const uint64_t mask = m_capacity - 1;
    uint64_t pos = m_header->readIndex.load(std::memory_order_relaxed);

    while (true) {
        ShmSlot& slot = m_slots[pos & mask];
        const uint64_t seq = slot.sequence.load(std::memory_order_acquire);
        const int64_t diff = static_cast<int64_t>(seq) - static_cast<int64_t>(pos + 1);

        if (diff == 0) {
            if (m_header->readIndex.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                if (wasFull) {
                    const uint64_t w = m_header->writeIndex.load(std::memory_order_relaxed);
                    *wasFull = (w - pos >= m_capacity);
                }
                const std::size_t length = slot.length;
                if (length > MaxPayloadBytes) {
                    slot.sequence.store(pos + m_capacity, std::memory_order_release);
                    m_corrupted.store(true, std::memory_order_relaxed);
                    logInvalidSlot("shared-memory slot length exceeds slot capacity", length, MaxPayloadBytes);
                    return 0;
                }
                if (length > destination.size()) {
                    slot.sequence.store(pos + m_capacity, std::memory_order_release);
                    logInvalidSlot("destination buffer is too small for shared-memory slot", length, destination.size());
                    return 0;
                }
                if (length > 0) {
                    std::memcpy(destination.data(), slot.payload, length);
                }
                slot.sequence.store(pos + m_capacity, std::memory_order_release);
                return length;
            }
        } else if (diff < 0) {
            return 0;
        } else {
            pos = m_header->readIndex.load(std::memory_order_relaxed);
        }
    }
}

std::size_t ShmRingBuffer::popBatch(
    Common::Compat::span<::Backend::BufferSlot> slots,
    bool* wasFull)
{
    if (!isValid() || slots.empty()) {
        return 0;
    }

    std::size_t count = 0;
    for (auto& slot : slots) {
        bool slotWasFull = false;
        const std::size_t n = popInto(slot.buffer, &slotWasFull);
        if (n == 0) {
            break;
        }
        slot.size = n;
        if (count == 0 && wasFull) {
            *wasFull = slotWasFull;
        }
        ++count;
    }
    return count;
}

std::size_t ShmRingBuffer::size() const noexcept
{
    if (!isValid()) {
        return 0;
    }
    const uint64_t w = m_header->writeIndex.load(std::memory_order_relaxed);
    const uint64_t r = m_header->readIndex.load(std::memory_order_relaxed);
    return (w >= r && w - r <= m_capacity) ? static_cast<std::size_t>(w - r) : 0;
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
    return size() >= m_capacity;
}

void ShmRingBuffer::reset()
{
    if (isValid()) {
        m_header->writeIndex.store(0, std::memory_order_release);
        m_header->readIndex.store(0, std::memory_order_release);
        for (uint32_t i = 0; i < m_capacity; ++i) {
            m_slots[i].sequence.store(i, std::memory_order_release);
            m_slots[i].length = 0;
        }
    }
}

const std::string& ShmRingBuffer::getPath() const noexcept
{
    return m_path;
}

void ShmRingBuffer::logInvalidSlot(std::string_view message, std::size_t length, std::size_t capacity)
{
    const uint64_t count = m_invalidSlotCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if (count <= 8 || (count & 1023) == 0) {
        Proxy::Log::warn("[ShmRingBuffer] {} (length: {}, capacity: {}, total dropped: {})",
            message, length, capacity, count);
    }
}

} // namespace Proxy::Backend::Shm
