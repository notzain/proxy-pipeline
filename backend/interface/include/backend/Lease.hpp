#pragma once

#include "backend/ILeaseState.hpp"
#include "backend/Settlement.hpp"
#include "backend/SettlementToken.hpp"
#include "common/memory/ObserverPtr.hpp"
#include <memory>

namespace Backend {

/**
 * @brief Move-only, out-of-band ownership of one transport-owned in-flight message.
 *
 * A Lease travels next to the received bytes (never inside them) and is settled exactly once.
 * Destroying an unsettled lease releases the message for redelivery, so an exception, shutdown,
 * or forgotten settlement never silently loses or acknowledges a message.
 *
 * Supports both polymorphic state (ILeaseState) and zero-allocation callbacks (SettlementToken).
 */
class Lease {
public:
    Lease() noexcept = default;
    explicit Lease(std::unique_ptr<ILeaseState> state) noexcept;
    explicit Lease(SettlementToken token) noexcept;

    Lease(Lease&& other) noexcept = default;
    Lease& operator=(Lease&& other) noexcept;
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;

    /**
     * @brief Releases the message for redelivery if still unsettled.
     */
    ~Lease();

    /**
     * @brief Settles with the given outcome. No-op if empty or already settled.
     */
    void settle(Settlement outcome) noexcept;

    void ack() noexcept;
    void reject() noexcept;
    void release() noexcept;

    /**
     * @brief Returns true if the lease still owns an unsettled message.
     */
    [[nodiscard]] bool active() const noexcept;
    explicit operator bool() const noexcept;

    /**
     * @brief Consumes and extracts the underlying transport lease state without settling it.
     */
    [[nodiscard]] std::unique_ptr<ILeaseState> releaseState() noexcept;

    /**
     * @brief Returns a non-owning observer pointer to the underlying lease state, or nullptr if inactive.
     */
    [[nodiscard]] Common::Memory::ObserverPtr<ILeaseState> state() const noexcept;

    /**
     * @brief Returns a non-owning pointer to the underlying SettlementToken, if present.
     */
    [[nodiscard]] SettlementToken* token() noexcept;
    [[nodiscard]] const SettlementToken* token() const noexcept;

private:
    std::unique_ptr<ILeaseState> m_state;
    SettlementToken m_token;
};

} // namespace Backend
