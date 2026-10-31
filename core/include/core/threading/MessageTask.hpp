#pragma once

#include "backend/Lease.hpp"
#include "backend/PayloadFormat.hpp"
#include "common/compat/Span.hpp"
#include "common/memory/BufferSlice.hpp"
#include "common/memory/DynamicBuffer.hpp"
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace Core::Threading {

/**
 * @brief Representation of a message task passed to background thread pool workers.
 *
 * Utilizes small buffer optimization (SBO) with a cache-friendly inline buffer (256 bytes)
 * for small frames, while larger payloads use a zero-copy pooled BufferSlice or heap fallback
 * held in a DynamicBuffer variant, or an externally owned leased view.
 */
class MessageTask {
public:
    static constexpr std::size_t SmallBufferSize = 256;

    MessageTask() = default;

    /**
     * @brief Assigns channel name and payload, using inline buffer if size <= SmallBufferSize.
     */
    void assign(std::string_view name, Common::Compat::span<const std::byte> data);

    /**
     * @brief Assigns a zero-copy pooled slice directly without heap allocation.
     */
    void assignSlice(std::string_view name, Common::Memory::BufferSlice slice, std::size_t size);

    /**
     * @brief Assigns a leased raw payload: an externally owned read-only view together with the Lease owning it.
     *
     * Binding both in one call guarantees the view can never outlive (or be detached from) its owner.
     */
    void assignLeased(std::string_view name, Common::Compat::span<const std::byte> view, Backend::Lease owner) noexcept;

    /**
     * @brief Constructs task with channel name and payload.
     */
    MessageTask(std::string name, Common::Compat::span<const std::byte> data);

    MessageTask(MessageTask&& other) noexcept;
    MessageTask& operator=(MessageTask&& other) noexcept;

    MessageTask(const MessageTask&) = delete;
    MessageTask& operator=(const MessageTask&) = delete;
    ~MessageTask() = default;

    /**
     * @brief Returns the channel name associated with the message.
     */
    [[nodiscard]] std::string_view channelName() const noexcept;

    /**
     * @brief Returns a non-owning span over the task payload data.
     */
    [[nodiscard]] Common::Compat::span<const std::byte> payload() const noexcept;

    /**
     * @brief Returns how payload() must be interpreted (`Raw` for leased views, `Enveloped` otherwise).
     */
    [[nodiscard]] Backend::PayloadFormat format() const noexcept;

    /**
     * @brief Attaches the transport ownership lease travelling with this message.
     */
    void setLease(Backend::Lease lease) noexcept;

    /**
     * @brief Returns the ownership lease so the consuming worker can move it out for settlement.
     *
     * Worker queues expose drained tasks as const views; the lease is settlement state rather than
     * message content, hence it is mutable.
     */
    [[nodiscard]] Backend::Lease& lease() const noexcept;

private:
    std::string m_channelName;
    std::array<std::byte, SmallBufferSize> m_inlineBuffer;
    std::size_t m_inlineSize { 0 };
    Common::Memory::DynamicBuffer m_spillBuffer;
    Common::Compat::span<const std::byte> m_leasedView { };
    bool m_leased { false };
    mutable Backend::Lease m_lease;
};

} // namespace Core::Threading
