#include "backend/Lease.hpp"
#include <utility>

namespace Backend {

Lease::Lease(SettlementToken token) noexcept
    : m_token(std::move(token))
{
}

Lease& Lease::operator=(Lease&& other) noexcept
{
    if (this != &other) {
        release();
        m_token = std::move(other.m_token);
    }
    return *this;
}

Lease::~Lease()
{
    release();
}

void Lease::settle(Settlement outcome) noexcept
{
    if (m_token.active()) {
        m_token.settle(outcome);
    }
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
    return m_token.active();
}

Lease::operator bool() const noexcept
{
    return active();
}

Common::Memory::ObserverPtr<SettlementToken> Lease::token() noexcept
{
    return m_token.active() ? Common::Memory::ObserverPtr<SettlementToken>(&m_token) : Common::Memory::ObserverPtr<SettlementToken> { };
}

Common::Memory::ObserverPtr<const SettlementToken> Lease::token() const noexcept
{
    return m_token.active() ? Common::Memory::ObserverPtr<const SettlementToken>(&m_token) : Common::Memory::ObserverPtr<const SettlementToken> { };
}

} // namespace Backend
