#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace Backend::Vendor::Sdk {

/**
 * @brief Stub representation of the vendor SDK's in-memory byte pointer + length struct.
 */
struct VendorBytePayload {
    const std::byte* data { nullptr };
    std::size_t length { 0 };
};

/**
 * @brief Stub representation of the proprietary 3rd-party VendorMessage.
 *
 * Exposes two mutually exclusive payload delivery methods matching the real vendor SDK:
 * 1. An in-memory byte pointer and length struct (`getBytePayload()`).
 * 2. A filesystem path to a payload file (`getPayloadFilePath()`).
 *
 * Replace the internals of this stub when integrating the actual vendor SDK.
 */
class VendorMessage {
public:
    VendorMessage() = default;

    /**
     * @brief Constructs an in-memory payload message (Mode A: byte pointer + length).
     */
    VendorMessage(std::uint64_t id, std::string payload)
        : m_id(id)
        , m_payload(std::move(payload))
    {
    }

    /**
     * @brief Constructs a file-backed payload message (Mode B: payload file path).
     */
    static VendorMessage fromFile(std::uint64_t id, std::string filePath)
    {
        VendorMessage msg;
        msg.m_id = id;
        msg.m_payloadFilePath = std::move(filePath);
        return msg;
    }

    [[nodiscard]] std::uint64_t getId() const noexcept
    {
        return m_id;
    }

    /**
     * @brief Method 1: Returns the in-memory byte pointer and length struct.
     *
     * Populated when the message payload resides in memory; `{ nullptr, 0 }` when file-backed.
     */
    [[nodiscard]] VendorBytePayload getBytePayload() const noexcept
    {
        if (!m_payloadFilePath.empty() || m_payload.empty()) {
            return { nullptr, 0 };
        }
        return {
            reinterpret_cast<const std::byte*>(m_payload.data()),
            m_payload.size()
        };
    }

    /**
     * @brief Method 2: Returns the payload file path on disk, or empty string if in-memory.
     */
    [[nodiscard]] const std::string& getPayloadFilePath() const noexcept
    {
        return m_payloadFilePath;
    }

    /**
     * @brief Convenience accessor returning the in-memory payload string.
     */
    [[nodiscard]] std::string getPayload() const
    {
        return m_payload;
    }

private:
    std::uint64_t m_id { 0 };
    std::string m_payload;
    std::string m_payloadFilePath;
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
