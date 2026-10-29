#pragma once

#include "backend/Settlement.hpp"

namespace Backend {

/**
 * @brief Transport-specific owner of exactly one in-flight message.
 *
 * Implemented by backends whose messages are owned by an external API (e.g. a vendor SDK)
 * and must be explicitly settled. The state object owns everything needed to settle the
 * message, so no registry of in-flight messages is required anywhere in the system.
 */
class ILeaseState {
public:
    virtual ~ILeaseState() = default;

    /**
     * @brief Settles the owned message. Invoked exactly once by Lease.
     */
    virtual void settle(Settlement outcome) noexcept = 0;
};

} // namespace Backend
