#pragma once

#include "backend/Settlement.hpp"
#include <utility>

namespace Backend {

/**
 * @brief Type-erased, zero-allocation RAII settlement token for in-flight message ownership.
 *
 * Replaces polymorphic heap-allocated leases with a flat, pointer-based completion handle.
 * Transports (e.g. VendorInputChannel) construct a SettlementToken pointing either to a
 * stack-frame context (for synchronous dispatch) or a pre-allocated stationary relay node
 * (for asynchronous queues), eliminating per-message malloc/free on the hot path.
 *
 * If destroyed while still active, SettlementToken settles with Settlement::Reject (or Release)
 * to guarantee that unhandled exceptions, premature shutdown, or dropped messages never leak.
 */
class SettlementToken {
public:
    using SettlerFn = void (*)(void* context, Settlement outcome) noexcept;
    using DeleterFn = void (*)(void* context) noexcept;

    constexpr SettlementToken() noexcept = default;

    /**
     * @brief Constructs an active token wrapping @p context.
     *
     * @param context Opaque pointer to the transport-specific state.
     * @param settler Callback invoked exactly once upon settlement.
     * @param deleter Optional callback invoked when the token is destroyed (e.g. recycling to a pool).
     */
    SettlementToken(
        void* context,
        SettlerFn settler,
        DeleterFn deleter = nullptr) noexcept;

    ~SettlementToken() noexcept;

    SettlementToken(SettlementToken&& other) noexcept;
    SettlementToken& operator=(SettlementToken&& other) noexcept;

    SettlementToken(const SettlementToken&) = delete;
    SettlementToken& operator=(const SettlementToken&) = delete;

    /**
     * @brief Settles the token with the given outcome. No-op if empty or already settled.
     */
    void settle(Settlement outcome) noexcept;

    void ack() noexcept;
    void reject() noexcept;
    void release() noexcept;

    /**
     * @brief Disarms the settler so post-processing does not settle (finalize/drop) the message.
     */
    void disarm() noexcept;

    /**
     * @brief Returns true if the token still owns an unsettled message.
     */
    [[nodiscard]] bool active() const noexcept;
    explicit operator bool() const noexcept;

    /**
     * @brief Returns the raw context pointer (non-owning).
     */
    [[nodiscard]] void* context() const noexcept;

private:
    void* m_context { nullptr };
    SettlerFn m_settler { nullptr };
    DeleterFn m_deleter { nullptr };
};

} // namespace Backend
