#pragma once

#include "backend/memory/Lease.hpp"
#include "backend/message/PayloadFormat.hpp"
#include "common/compat/Span.hpp"
#include <cstddef>

namespace Backend {

/**
 * @brief Cohesive target buffer, received byte counter and ownership lease for vectorized batch ingress.
 *
 * A slot is in exactly one of two states:
 * - **Written** (default): the transport copied an enveloped frame into `buffer`; `size` holds the byte count.
 * - **Leased**: the transport exposes externally owned, read-only raw payload memory whose lifetime is
 *   bound to `lease` (see `assignLeased()`). `buffer` is untouched.
 *
 * Consumers (execution strategies) call `reset()` before every receive so no stale leased view can
 * survive into the next message. Transports owning their buffers leave `lease` empty.
 */
struct BufferSlot {
    Common::Compat::span<std::byte> buffer { };
    std::size_t size { 0 };
    Lease lease { };

    BufferSlot() noexcept = default;

    /**
     * @brief Constructs a written-mode slot targeting @p target.
     */
    BufferSlot(Common::Compat::span<std::byte> target, std::size_t filled = 0) noexcept;

    BufferSlot(BufferSlot&&) noexcept = default;
    BufferSlot& operator=(BufferSlot&&) noexcept = default;

    /**
     * @brief Returns the slot to the empty written state, releasing any lease still attached.
     */
    void reset() noexcept;

    /**
     * @brief Switches the slot to leased mode, exposing @p view (owned by @p owner) as a raw payload.
     */
    void assignLeased(Common::Compat::span<const std::byte> view, Lease owner) noexcept;

    /**
     * @brief Returns true if the slot references leased external memory rather than `buffer`.
     */
    [[nodiscard]] bool isLeased() const noexcept;

    /**
     * @brief Returns how the populated bytes must be interpreted (`Raw` when leased, `Enveloped` otherwise).
     */
    [[nodiscard]] PayloadFormat format() const noexcept;

    /**
     * @brief Returns a non-owning span over the populated bytes (leased view or filled prefix of `buffer`).
     */
    [[nodiscard]] Common::Compat::span<const std::byte> filledSpan() const noexcept;

private:
    Common::Compat::span<const std::byte> m_leasedView { };
    bool m_leased { false };
};

} // namespace Backend
