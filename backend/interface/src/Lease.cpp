#include "backend/Lease.hpp"
#include <utility>

namespace Backend {

Lease::Lease(std::unique_ptr<ILeaseState> state) noexcept
    : m_state(std::move(state))
{
}

Lease& Lease::operator=(Lease&& other) noexcept
{
    if (this != &other) {
        release();
        m_state = std::move(other.m_state);
    }
    return *this;
}

Lease::~Lease()
{
    release();
}

void Lease::settle(Settlement outcome) noexcept
{
    auto state = std::move(m_state);
    if (!state) {
        return;
    }
    state->settle(outcome);
}

void Lease::ack() noexcept
{
    settle(Settlement::Ack);
}

void Lease::reject() noexcept
{
    settle(Settlement::Reject);
}

void Lease::release() noexcept
{
    settle(Settlement::Release);
}

bool Lease::active() const noexcept
{
    return m_state != nullptr;
}

Lease::operator bool() const noexcept
{
    return active();
}

} // namespace Backend
