#include "backend/memory/SettlementToken.hpp"

namespace Backend {

SettlementToken::SettlementToken(
    void* context,
    SettlerFn settler,
    DeleterFn deleter) noexcept
    : m_context(context)
    , m_settler(settler)
    , m_deleter(deleter)
{
}

SettlementToken::~SettlementToken() noexcept
{
    if (m_context) {
        if (m_settler) {
            m_settler(m_context, Settlement::Reject);
            m_settler = nullptr;
        }
        if (m_deleter) {
            m_deleter(m_context);
            m_deleter = nullptr;
        }
        m_context = nullptr;
    }
}

SettlementToken::SettlementToken(SettlementToken&& other) noexcept
    : m_context(std::exchange(other.m_context, nullptr))
    , m_settler(std::exchange(other.m_settler, nullptr))
    , m_deleter(std::exchange(other.m_deleter, nullptr))
{
}

SettlementToken& SettlementToken::operator=(SettlementToken&& other) noexcept
{
    if (this != &other) {
        settle(Settlement::Reject);
        if (m_context && m_deleter) {
            m_deleter(m_context);
        }
        m_context = std::exchange(other.m_context, nullptr);
        m_settler = std::exchange(other.m_settler, nullptr);
        m_deleter = std::exchange(other.m_deleter, nullptr);
    }
    return *this;
}

void SettlementToken::settle(Settlement outcome) noexcept
{
    if (m_context && m_settler) {
        auto* const ctx = m_context;
        auto const fn = m_settler;
        m_settler = nullptr;
        fn(ctx, outcome);
    }
}

void SettlementToken::ack() noexcept
{
    settle(Settlement::Ack);
}

void SettlementToken::reject() noexcept
{
    settle(Settlement::Reject);
}

void SettlementToken::release() noexcept
{
    settle(Settlement::Release);
}

void SettlementToken::disarm() noexcept
{
    m_settler = nullptr;
}

bool SettlementToken::active() const noexcept
{
    return m_context != nullptr && m_settler != nullptr;
}

SettlementToken::operator bool() const noexcept
{
    return active();
}

void* SettlementToken::context() const noexcept
{
    return m_context;
}

SettlementToken::SettlerFn SettlementToken::settler() const noexcept
{
    return m_settler;
}

} // namespace Backend
