#pragma once

#include "backend/vendor/VendorSdkStub.hpp"
#include <utility>

namespace Backend::Vendor {

/**
 * @brief RAII scoped guard ensuring an in-flight VendorMessage is safely finalized or dropped.
 */
class ScopedVendorMessage {
public:
    ScopedVendorMessage(Sdk::VendorMessage msg, Sdk::VendorManager& manager)
        : m_msg(std::move(msg))
        , m_manager(&manager)
    {
    }

    ~ScopedVendorMessage()
    {
        if (!m_resolved && m_manager) {
            m_manager->drop(m_msg);
        }
    }

    ScopedVendorMessage(ScopedVendorMessage&& other) noexcept
        : m_msg(std::move(other.m_msg))
        , m_manager(other.m_manager)
        , m_resolved(other.m_resolved)
    {
        other.m_resolved = true;
    }

    ScopedVendorMessage& operator=(ScopedVendorMessage&& other) noexcept
    {
        if (this != &other) {
            if (!m_resolved && m_manager) {
                m_manager->drop(m_msg);
            }
            m_msg = std::move(other.m_msg);
            m_manager = other.m_manager;
            m_resolved = other.m_resolved;
            other.m_resolved = true;
        }
        return *this;
    }

    ScopedVendorMessage(const ScopedVendorMessage&) = delete;
    ScopedVendorMessage& operator=(const ScopedVendorMessage&) = delete;

    [[nodiscard]] std::uint64_t getId() const noexcept
    {
        return m_msg.getId();
    }

    [[nodiscard]] std::string getPayload() const
    {
        return m_msg.getPayload();
    }

    [[nodiscard]] const Sdk::VendorMessage& rawMessage() const noexcept
    {
        return m_msg;
    }

    void finalize()
    {
        if (!m_resolved && m_manager) {
            m_manager->finalize(m_msg);
            m_resolved = true;
        }
    }

    void drop()
    {
        if (!m_resolved && m_manager) {
            m_manager->drop(m_msg);
            m_resolved = true;
        }
    }

private:
    Sdk::VendorMessage m_msg;
    Sdk::VendorManager* m_manager { nullptr };
    bool m_resolved { false };
};

} // namespace Backend::Vendor
