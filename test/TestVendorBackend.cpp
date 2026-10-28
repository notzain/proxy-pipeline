#include <catch2/catch_test_macros.hpp>
#include "backend/vendor/ScopedVendorMessage.hpp"
#include "backend/vendor/VendorBackend.hpp"
#include "backend/vendor/VendorInputChannel.hpp"
#include "backend/vendor/VendorMetadata.hpp"
#include "backend/vendor/VendorOutputChannel.hpp"
#include "core/handler/HandlerRunner.hpp"
#include "core/handler/IHandler.hpp"
#include "backend/MessageResult.hpp"
#include "backend/MessageView.hpp"
#include "core/threading/SimpleExecutionStrategy.hpp"
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace {

class MockVendorReceiver : public Backend::Vendor::Sdk::VendorReceiver {
public:
    void enqueue(std::uint64_t id, std::string payload)
    {
        m_queue.emplace_back(id, std::move(payload));
    }

    bool receive(Backend::Vendor::Sdk::VendorMessage* outMsg, int /*timeoutMs*/) override
    {
        if (m_queue.empty()) {
            return false;
        }
        *outMsg = std::move(m_queue.front());
        m_queue.pop_front();
        return true;
    }

private:
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
        finalizedIds.push_back(msg.getId());
    }

    void drop(const Backend::Vendor::Sdk::VendorMessage& msg) override
    {
        droppedIds.push_back(msg.getId());
    }

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
        receivedPayloads.push_back(std::string(
            reinterpret_cast<const char*>(message.payload().data()),
            message.payload().size()));

        if (m_shouldDrop) {
            return Backend::Dropped { "Test rejection", 400 };
        }
        return Backend::Consumed {};
    }

    std::vector<std::string> receivedPayloads;
    bool m_shouldDrop { false };
};

} // namespace

TEST_CASE("VendorBackend Option 1 Integration: MsgId in Metadata & Finalize/Drop Lifecycle", "[vendor][backend]")
{
    auto mockManager = std::make_shared<MockVendorManager>();
    auto mockReceiver = std::make_shared<MockVendorReceiver>();
    auto mockSender = std::make_shared<MockVendorSender>();

    Backend::Vendor::VendorBackend backend(mockManager);

    SECTION("Successful processing finalizes VendorMessage by ID")
    {
        VendorTestHandler handler(backend, /*shouldDrop=*/false);
        auto inputChannel = std::make_unique<Backend::Vendor::VendorInputChannel>("vendor_in", backend, mockReceiver);
        handler.registerInputChannel(std::move(inputChannel));

        mockReceiver->enqueue(1001, "Vendor Order 1001 Data");

        Core::Handler::HandlerRunner runner(handler);
        runner.setExecutionStrategy(std::make_unique<Core::Threading::SimpleExecutionStrategy>());

        runner.startAsync(false);

        auto start = std::chrono::steady_clock::now();
        while (handler.receivedPayloads.empty() &&
               std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        runner.stop();
        runner.wait();

        REQUIRE(handler.receivedPayloads.size() == 1);
        REQUIRE(handler.receivedPayloads.front() == "Vendor Order 1001 Data");

        // Verified: VendorBackend received onMessageResult and finalized the exact ID!
        REQUIRE(mockManager->finalizedIds.size() == 1);
        REQUIRE(mockManager->finalizedIds.front() == 1001);
        REQUIRE(mockManager->droppedIds.empty());
    }

    SECTION("Rejected processing drops VendorMessage by ID")
    {
        VendorTestHandler handler(backend, /*shouldDrop=*/true);
        auto inputChannel = std::make_unique<Backend::Vendor::VendorInputChannel>("vendor_in", backend, mockReceiver);
        handler.registerInputChannel(std::move(inputChannel));

        mockReceiver->enqueue(2002, "Malformed Vendor Data");

        Core::Handler::HandlerRunner runner(handler);
        runner.setExecutionStrategy(std::make_unique<Core::Threading::SimpleExecutionStrategy>());

        runner.startAsync(false);

        auto start = std::chrono::steady_clock::now();
        while (handler.receivedPayloads.empty() &&
               std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        runner.stop();
        runner.wait();

        REQUIRE(handler.receivedPayloads.size() == 1);

        // Verified: VendorBackend received onMessageResult (Dropped) and dropped the exact ID!
        REQUIRE(mockManager->droppedIds.size() == 1);
        REQUIRE(mockManager->droppedIds.front() == 2002);
        REQUIRE(mockManager->finalizedIds.empty());
    }
}

