#pragma once

#include <cstdint>
#include <string>

namespace Backend::Vendor::Sdk {

/**
 * @brief Stub representation of the proprietary 3rd-party VendorMessage.
 *
 * Replace the internals of this stub when integrating the actual vendor SDK.
 */
class VendorMessage {
public:
    VendorMessage() = default;
    VendorMessage(std::uint64_t id, std::string payload)
        : m_id(id)
        , m_payload(std::move(payload))
    {
    }

    [[nodiscard]] std::uint64_t getId() const noexcept
    {
        return m_id;
    }

    [[nodiscard]] std::string getPayload() const
    {
        return m_payload;
    }

private:
    std::uint64_t m_id { 0 };
    std::string m_payload;
};

/**
 * @brief Stub representation of the 3rd-party vendor Receiver.
 */
class VendorReceiver {
public:
    virtual ~VendorReceiver() = default;
    virtual bool receive(VendorMessage* outMsg, int timeoutMs) = 0;
};

/**
 * @brief Stub representation of the 3rd-party vendor Sender.
 */
class VendorSender {
public:
    virtual ~VendorSender() = default;
    virtual bool send(const VendorMessage& msg) = 0;
};

/**
 * @brief Stub representation of the 3rd-party vendor Management Object.
 */
class VendorManager {
public:
    virtual ~VendorManager() = default;

    virtual void finalize(const VendorMessage& msg)
    {
        // Actual vendor API: vendorManager.finalize(msg)
        (void)msg;
    }

    virtual void drop(const VendorMessage& msg)
    {
        // Actual vendor API: vendorManager.drop(msg)
        (void)msg;
    }
};

} // namespace Backend::Vendor::Sdk
