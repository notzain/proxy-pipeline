#include "backend/MessageEnvelope.hpp"
#include "backend/MessageResult.hpp"
#include "backend/MessageView.hpp"
#include "backend/vendor/VendorBackend.hpp"
#include "backend/vendor/VendorInputChannel.hpp"
#include "backend/vendor/VendorOutputChannel.hpp"
#include "common/memory/ByteView.hpp"
#include "common/memory/MutableByteView.hpp"
#include "core/handler/HandlerRunner.hpp"
#include "core/handler/IHandler.hpp"
#include "core/threading/SequentialStrategy.hpp"
#include "core/threading/SimpleExecutionStrategy.hpp"
#include "core/threading/ThreadPoolStrategy.hpp"
#include <catch2/catch_test_macros.hpp>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

class MockVendorReceiver : public Backend::Vendor::Sdk::VendorReceiver {
public:
    void enqueue(std::uint64_t id, std::string payload)
    {
        std::lock_guard lock(m_mutex);
        m_queue.emplace_back(id, std::move(payload));
    }

    bool receive(Backend::Vendor::Sdk::VendorMessage* outMsg, int /*timeoutMs*/) override
    {
        std::lock_guard lock(m_mutex);
        if (m_queue.empty()) {
            return false;
        }
        *outMsg = std::move(m_queue.front());
        m_queue.pop_front();
        return true;
    }

private:
    std::mutex m_mutex;
    std::deque<Backend::Vendor::Sdk::VendorMessage> m_queue;
};

class MockVendorSender : public Backend::Vendor::Sdk::VendorSender {
public:
    bool send(const Backend::Vendor::Sdk::VendorMessage& msg) override
    {
        sentPayloads.push_back(msg.getPayload());
        return true;
    }

    std::vector<std::string> sentPayloads;
};

class MockVendorManager : public Backend::Vendor::Sdk::VendorManager {
public:
    void finalize(const Backend::Vendor::Sdk::VendorMessage& msg) override
    {
        std::lock_guard lock(m_mutex);
        finalizedIds.push_back(msg.getId());
    }

    void drop(const Backend::Vendor::Sdk::VendorMessage& msg) override
    {
        std::lock_guard lock(m_mutex);
        droppedIds.push_back(msg.getId());
    }

    [[nodiscard]] std::size_t finalizedCount() const
    {
        std::lock_guard lock(m_mutex);
        return finalizedIds.size();
    }

    [[nodiscard]] std::size_t droppedCount() const
    {
        std::lock_guard lock(m_mutex);
        return droppedIds.size();
    }

    mutable std::mutex m_mutex;
    std::vector<std::uint64_t> finalizedIds;
    std::vector<std::uint64_t> droppedIds;
};

class VendorTestHandler : public Core::Handler::IHandler {
public:
    explicit VendorTestHandler(Backend::IBackend& backend, bool shouldDrop = false)
        : Core::Handler::IHandler(backend)
        , m_shouldDrop(shouldDrop)
    {
    }

    Backend::MessageResult onMessageReceived(
        std::string_view /*channelName*/,
        const Backend::MessageView& message) override
    {
        {
            std::lock_guard lock(m_mutex);
            receivedPayloads.emplace_back(
                reinterpret_cast<const char*>(message.payload().data()),
                message.payload().size());
            receivedMetadataSizes.push_back(message.metadata().size());
        }

        if (m_shouldDrop) {
            return Backend::Dropped { "Test rejection", 400 };
        }
        return Backend::Consumed { };
    }

    [[nodiscard]] std::size_t receivedCount() const
    {
        std::lock_guard lock(m_mutex);
        return receivedPayloads.size();
    }

    mutable std::mutex m_mutex;
    std::vector<std::string> receivedPayloads;
    std::vector<std::size_t> receivedMetadataSizes;
    bool m_shouldDrop { false };
};

} // namespace

TEST_CASE("VendorBackend Out-of-Band Lease Ownership & Settlement Lifecycle", "[vendor][backend]")
{
    auto mockManager = std::make_shared<MockVendorManager>();
    auto mockReceiver = std::make_shared<MockVendorReceiver>();
    auto mockSender = std::make_shared<MockVendorSender>();

    Backend::Vendor::VendorBackend backend(mockManager);

    SECTION("Successful processing settles Lease with Ack and finalizes VendorMessage without wire metadata")
    {
        VendorTestHandler handler(backend, /*shouldDrop=*/false);
        auto inputChannel = std::make_unique<Backend::Vendor::VendorInputChannel>("vendor_in", backend, mockReceiver);
        handler.registerInputChannel(std::move(inputChannel));

        mockReceiver->enqueue(1001, "Vendor Order 1001 Data");

        Core::Handler::HandlerRunner runner(handler);
        runner.setExecutionStrategy(std::make_unique<Core::Threading::SimpleExecutionStrategy>());

        runner.startAsync(false);

        auto start = std::chrono::steady_clock::now();
        while (mockManager->finalizedCount() == 0 && std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        runner.stop();
        runner.wait();

        REQUIRE(handler.receivedPayloads.size() == 1);
        REQUIRE(handler.receivedPayloads.front() == "Vendor Order 1001 Data");
        REQUIRE(handler.receivedMetadataSizes.front() == 0);

        REQUIRE(mockManager->finalizedIds.size() == 1);
        REQUIRE(mockManager->finalizedIds.front() == 1001);
        REQUIRE(mockManager->droppedIds.empty());
    }

    SECTION("Rejected processing settles Lease with Reject and drops VendorMessage")
    {
        VendorTestHandler handler(backend, /*shouldDrop=*/true);
        auto inputChannel = std::make_unique<Backend::Vendor::VendorInputChannel>("vendor_in", backend, mockReceiver);
        handler.registerInputChannel(std::move(inputChannel));

        mockReceiver->enqueue(2002, "Malformed Vendor Data");

        Core::Handler::HandlerRunner runner(handler);
        runner.setExecutionStrategy(std::make_unique<Core::Threading::SimpleExecutionStrategy>());

        runner.startAsync(false);

        auto start = std::chrono::steady_clock::now();
        while (mockManager->droppedCount() == 0 && std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        runner.stop();
        runner.wait();

        REQUIRE(handler.receivedPayloads.size() == 1);
        REQUIRE(mockManager->droppedIds.size() == 1);
        REQUIRE(mockManager->droppedIds.front() == 2002);
        REQUIRE(mockManager->finalizedIds.empty());
    }

    SECTION("ThreadPoolStrategy propagates Lease across worker queue and settles on worker completion")
    {
        VendorTestHandler handler(backend, /*shouldDrop=*/false);
        auto inputChannel = std::make_unique<Backend::Vendor::VendorInputChannel>("vendor_in", backend, mockReceiver);
        handler.registerInputChannel(std::move(inputChannel));

        mockReceiver->enqueue(3001, "Pool Msg 1");
        mockReceiver->enqueue(3002, "Pool Msg 2");

        Core::Handler::HandlerRunner runner(handler);
        runner.setExecutionStrategy(std::make_unique<Core::Threading::ThreadPoolStrategy>(2, true, true));

        runner.startAsync(false);

        auto start = std::chrono::steady_clock::now();
        while (mockManager->finalizedCount() < 2 && std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        runner.stop();
        runner.wait();

        REQUIRE(handler.receivedCount() == 2);
        REQUIRE(mockManager->finalizedIds.size() == 2);
        REQUIRE(mockManager->droppedIds.empty());
    }

    SECTION("Unsettled Lease automatically releases/drops VendorMessage on destruction")
    {
        Backend::Vendor::VendorInputChannel inputChannel("vendor_in", backend, mockReceiver);
        mockReceiver->enqueue(4004, "Unprocessed Payload");

        std::array<std::byte, 256> buffer { };
        {
            Backend::BufferSlot slot { Common::Memory::MutableByteView(buffer), 0 };
            auto result = inputChannel.receiveSlot(slot, std::chrono::milliseconds(10));
            REQUIRE(result.hasValue());
            REQUIRE(slot.lease.active());
            REQUIRE(mockManager->droppedIds.empty());
            REQUIRE(mockManager->finalizedIds.empty());
        }

        REQUIRE(mockManager->droppedIds.size() == 1);
        REQUIRE(mockManager->droppedIds.front() == 4004);
        REQUIRE(mockManager->finalizedIds.empty());
    }

    SECTION("VendorOutputChannel strips envelope header when sending enveloped message")
    {
        Backend::Vendor::VendorOutputChannel outputChannel("vendor_out", mockSender);
        const std::string payload = "Clean Vendor Egress Payload";
        auto envelope = Backend::MessageEnvelope::packRaw(Common::Memory::ByteView(payload));

        REQUIRE(outputChannel.send(Common::Memory::ByteView(envelope)));
        REQUIRE(mockSender->sentPayloads.size() == 1);
        REQUIRE(mockSender->sentPayloads.front() == payload);
    }
}
