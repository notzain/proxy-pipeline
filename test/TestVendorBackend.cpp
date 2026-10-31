#include "backend/message/MessageResult.hpp"
#include "backend/message/MessageView.hpp"
#include "backend/vendor/VendorBackend.hpp"
#include "backend/vendor/VendorInputChannel.hpp"
#include "backend/vendor/VendorOutputChannel.hpp"
#include "common/memory/ByteView.hpp"
#include "common/memory/MutableByteView.hpp"
#include "core/handler/HandlerRunner.hpp"
#include "core/handler/IHandler.hpp"
#include "core/threading/DedicatedThreadStrategy.hpp"
#include "core/threading/SequentialStrategy.hpp"
#include "core/threading/SimpleExecutionStrategy.hpp"
#include "core/threading/ThreadPoolStrategy.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
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

    void enqueueFile(std::uint64_t id, std::string filePath)
    {
        std::lock_guard lock(m_mutex);
        m_queue.push_back(Backend::Vendor::Sdk::VendorMessage::fromFile(id, std::move(filePath)));
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
        std::lock_guard lock(m_mutex);
        if (failSends) {
            return false;
        }
        sentPayloads.push_back(msg.getPayload());
        sentMessages.push_back(msg);
        return true;
    }

    [[nodiscard]] std::size_t sentCount() const
    {
        std::lock_guard lock(m_mutex);
        return sentMessages.size();
    }

    mutable std::mutex m_mutex;
    bool failSends { false };
    std::vector<std::string> sentPayloads;
    std::vector<Backend::Vendor::Sdk::VendorMessage> sentMessages;
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

class VendorForwardHandler : public Core::Handler::IHandler {
public:
    explicit VendorForwardHandler(Backend::IBackend& backend)
        : Core::Handler::IHandler(backend)
    {
    }

    void addOutputChannel(Common::Memory::Borrow<Backend::IOutputChannel> out)
    {
        m_outputs.push_back(out);
    }

    Backend::MessageResult onMessageReceived(
        std::string_view /*channelName*/,
        const Backend::MessageView& message) override
    {
        for (auto out : m_outputs) {
            out->send(message);
        }
        return Backend::Forwarded { .channel = m_outputs.empty() ? std::string_view { } : m_outputs.front()->name() };
    }

private:
    std::vector<Common::Memory::Borrow<Backend::IOutputChannel>> m_outputs;
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
        Backend::Vendor::VendorOutputChannel outputChannel("vendor_out", mockManager, mockSender);
        const std::string payload = "Clean Vendor Egress Payload";
        auto envelope = Backend::MessageView::packRaw(Common::Memory::ByteView(payload));

        REQUIRE(outputChannel.send(Common::Memory::ByteView(envelope)));
        REQUIRE(mockSender->sentPayloads.size() == 1);
        REQUIRE(mockSender->sentPayloads.front() == payload);
    }

    SECTION("Failed consuming send leaves Lease armed so settlement still finalizes the VendorMessage")
    {
        Backend::Vendor::VendorInputChannel inputChannel("vendor_in", backend, mockReceiver);
        Backend::Vendor::VendorOutputChannel outputChannel("vendor_out", mockManager, mockSender);
        mockReceiver->enqueue(4501, "Send Fails");
        mockSender->failSends = true;

        Backend::BufferSlot slot { };
        REQUIRE(inputChannel.receiveSlot(slot, std::chrono::milliseconds(10)).hasValue());
        Backend::MessageView view(slot.filledSpan());
        view.attachLease(slot.lease);

        CHECK_FALSE(outputChannel.send(view));
        REQUIRE(slot.lease.active());
        slot.lease.ack();

        REQUIRE(mockManager->finalizedIds.size() == 1);
        CHECK(mockManager->finalizedIds.front() == 4501);
        CHECK(mockManager->droppedIds.empty());
    }

    SECTION("Consuming forward is refused across different VendorManagers and falls back to a payload copy")
    {
        auto otherManager = std::make_shared<MockVendorManager>();
        Backend::Vendor::VendorInputChannel inputChannel("vendor_in", backend, mockReceiver);
        Backend::Vendor::VendorOutputChannel outputChannel("vendor_out", otherManager, mockSender);
        mockReceiver->enqueue(4502, "Cross Manager");

        Backend::BufferSlot slot { };
        REQUIRE(inputChannel.receiveSlot(slot, std::chrono::milliseconds(10)).hasValue());
        Backend::MessageView view(slot.filledSpan());
        view.attachLease(slot.lease);

        REQUIRE(outputChannel.send(view));
        REQUIRE(mockSender->sentMessages.size() == 1);
        CHECK(mockSender->sentMessages.front().getId() == 0);
        CHECK(mockSender->sentPayloads.front() == "Cross Manager");

        slot.lease.ack();
        REQUIRE(mockManager->finalizedIds.size() == 1);
        CHECK(mockManager->finalizedIds.front() == 4502);
    }

    SECTION("Raw leased payload resembling a Proxy envelope is delivered verbatim and never parsed")
    {
        VendorTestHandler handler(backend, /*shouldDrop=*/false);
        handler.registerInputChannel(std::make_unique<Backend::Vendor::VendorInputChannel>("vendor_in", backend, mockReceiver));

        const auto envelope = Backend::MessageView::packRaw(Common::Memory::ByteView(std::string_view("inner")));
        const std::string envelopeBytes(reinterpret_cast<const char*>(envelope.data()), envelope.size());
        mockReceiver->enqueue(4503, envelopeBytes);

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
        CHECK(handler.receivedPayloads.front() == envelopeBytes);
    }

    SECTION("Consuming forward sends exact received VendorMessage object and skips finalize/drop")
    {
        VendorForwardHandler handler(backend);
        auto inputChannel = std::make_unique<Backend::Vendor::VendorInputChannel>("vendor_in", backend, mockReceiver);
        auto outputChannel = std::make_unique<Backend::Vendor::VendorOutputChannel>("vendor_out", mockManager, mockSender);
        handler.registerInputChannel(std::move(inputChannel));
        handler.addOutputChannel(handler.registerOutputChannel(std::move(outputChannel)));

        mockReceiver->enqueue(5005, "Forwarded Vendor Order 5005");

        Core::Handler::HandlerRunner runner(handler);
        runner.setExecutionStrategy(std::make_unique<Core::Threading::SimpleExecutionStrategy>());

        runner.startAsync(false);

        auto start = std::chrono::steady_clock::now();
        while (mockSender->sentCount() == 0 && std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        runner.stop();
        runner.wait();

        REQUIRE(mockSender->sentMessages.size() == 1);
        CHECK(mockSender->sentMessages.front().getId() == 5005);
        CHECK(mockSender->sentMessages.front().getPayload() == "Forwarded Vendor Order 5005");

        CHECK(mockManager->finalizedIds.empty());
        CHECK(mockManager->droppedIds.empty());
    }

    SECTION("Rebound payload via view.withPayload() sends modified payload and settles original VendorMessage with Ack")
    {
        Backend::Vendor::VendorInputChannel inputChannel("vendor_in", backend, mockReceiver);
        Backend::Vendor::VendorOutputChannel outputChannel("vendor_out", mockManager, mockSender);
        mockReceiver->enqueue(6006, "Original Vendor Payload");

        Backend::BufferSlot slot { };
        REQUIRE(inputChannel.receiveSlot(slot, std::chrono::milliseconds(10)).hasValue());
        Backend::MessageView view(slot.filledSpan());
        view.attachLease(slot.lease);

        const std::string modified = "Transformed Vendor Payload";
        auto rebound = view.withPayload(Common::Memory::ByteView(modified));
        REQUIRE(outputChannel.send(rebound));

        // Original lease remains active and is settled with Ack by processBatch
        REQUIRE(slot.lease.active());
        slot.lease.ack();

        REQUIRE(mockSender->sentPayloads.size() == 1);
        CHECK(mockSender->sentPayloads.front() == modified);
        REQUIRE(mockManager->finalizedIds.size() == 1);
        CHECK(mockManager->finalizedIds.front() == 6006);
    }

    SECTION("Fan-out to multiple output channels consumes original VendorMessage once and creates new message for remaining channels")
    {
        auto secondSender = std::make_shared<MockVendorSender>();
        VendorForwardHandler handler(backend);
        auto inputChannel = std::make_unique<Backend::Vendor::VendorInputChannel>("vendor_in", backend, mockReceiver);
        auto outChan1 = std::make_unique<Backend::Vendor::VendorOutputChannel>("vendor_out_1", mockManager, mockSender);
        auto outChan2 = std::make_unique<Backend::Vendor::VendorOutputChannel>("vendor_out_2", mockManager, secondSender);

        handler.registerInputChannel(std::move(inputChannel));
        handler.addOutputChannel(handler.registerOutputChannel(std::move(outChan1)));
        handler.addOutputChannel(handler.registerOutputChannel(std::move(outChan2)));

        mockReceiver->enqueue(7007, "Fanout Vendor Payload");

        Core::Handler::HandlerRunner runner(handler);
        runner.setExecutionStrategy(std::make_unique<Core::Threading::SimpleExecutionStrategy>());

        runner.startAsync(false);

        auto start = std::chrono::steady_clock::now();
        while (secondSender->sentCount() == 0 && std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        runner.stop();
        runner.wait();

        REQUIRE(mockSender->sentMessages.size() == 1);
        CHECK(mockSender->sentMessages.front().getId() == 7007);
        CHECK(mockSender->sentMessages.front().getPayload() == "Fanout Vendor Payload");

        REQUIRE(secondSender->sentMessages.size() == 1);
        CHECK(secondSender->sentMessages.front().getId() == 0);
        CHECK(secondSender->sentMessages.front().getPayload() == "Fanout Vendor Payload");

        CHECK(mockManager->finalizedIds.empty());
        CHECK(mockManager->droppedIds.empty());
    }

    SECTION("Mode A (byte pointer + length) delivers oversized >64KB payload zero-copy across ThreadPoolStrategy without truncation")
    {
        VendorTestHandler handler(backend, /*shouldDrop=*/false);
        auto inputChannel = std::make_unique<Backend::Vendor::VendorInputChannel>("vendor_in", backend, mockReceiver);
        handler.registerInputChannel(std::move(inputChannel));

        const std::string largePayload(100000, 'Z'); // 100 KB (> 2048 B SBO and > 65535 B batch buffer)
        mockReceiver->enqueue(8001, largePayload);

        Core::Handler::HandlerRunner runner(handler);
        runner.setExecutionStrategy(std::make_unique<Core::Threading::ThreadPoolStrategy>(2, false, true));

        runner.startAsync(false);

        auto start = std::chrono::steady_clock::now();
        while (mockManager->finalizedCount() == 0 && std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        runner.stop();
        runner.wait();

        REQUIRE(handler.receivedPayloads.size() == 1);
        CHECK(handler.receivedPayloads.front().size() == largePayload.size());
        CHECK(handler.receivedPayloads.front() == largePayload);
        REQUIRE(mockManager->finalizedIds.size() == 1);
        CHECK(mockManager->finalizedIds.front() == 8001);
    }

    SECTION("Mode B (payload file path) memory-maps file with zero allocations and releases mapping on settle")
    {
        Backend::Vendor::VendorBackend vendorBackend(mockManager);

        const auto tempFile = std::filesystem::temp_directory_path() / "vendor_mode_b_pooled_test.dat";
        const std::string fileContent = "File-backed vendor payload mapped via MappedFile zero-copy";
        {
            std::ofstream out(tempFile, std::ios::binary);
            out.write(fileContent.data(), static_cast<std::streamsize>(fileContent.size()));
        }

        Backend::Vendor::VendorInputChannel inputChannel("vendor_in", vendorBackend, mockReceiver);
        mockReceiver->enqueueFile(9001, tempFile.string());

        {
            Backend::BufferSlot slot { };
            auto res = inputChannel.receiveSlot(slot, std::chrono::milliseconds(10));
            REQUIRE(res.hasValue());
            CHECK(res.value() == fileContent.size());
            CHECK(slot.isLeased());

            const auto view = slot.filledSpan();
            std::string receivedStr(reinterpret_cast<const char*>(view.data()), view.size());
            CHECK(receivedStr == fileContent);

            slot.lease.ack();
        }

        REQUIRE(mockManager->finalizedIds.size() == 1);
        CHECK(mockManager->finalizedIds.front() == 9001);

        std::filesystem::remove(tempFile);
    }

    SECTION("Mode B (oversized payload file path) maps file directly and forwards original file-backed VendorMessage")
    {
        Backend::Vendor::VendorBackend vendorBackend(mockManager);

        const auto tempFile = std::filesystem::temp_directory_path() / "vendor_mode_b_oversized_test.dat";
        const std::string largeFileContent(8192, 'F');
        {
            std::ofstream out(tempFile, std::ios::binary);
            out.write(largeFileContent.data(), static_cast<std::streamsize>(largeFileContent.size()));
        }

        VendorForwardHandler handler(vendorBackend);
        auto inputChannel = std::make_unique<Backend::Vendor::VendorInputChannel>("vendor_in", vendorBackend, mockReceiver);
        auto outputChannel = std::make_unique<Backend::Vendor::VendorOutputChannel>("vendor_out", mockManager, mockSender);
        handler.registerInputChannel(std::move(inputChannel));
        handler.addOutputChannel(handler.registerOutputChannel(std::move(outputChannel)));

        mockReceiver->enqueueFile(9002, tempFile.string());

        Core::Handler::HandlerRunner runner(handler);
        runner.setExecutionStrategy(std::make_unique<Core::Threading::DedicatedThreadStrategy>(true));

        runner.startAsync(false);

        auto start = std::chrono::steady_clock::now();
        while (mockSender->sentCount() == 0 && std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        runner.stop();
        runner.wait();

        REQUIRE(mockSender->sentMessages.size() == 1);
        CHECK(mockSender->sentMessages.front().getId() == 9002);
        CHECK(mockSender->sentMessages.front().getPayloadFilePath() == tempFile.string());
        CHECK(mockManager->finalizedIds.empty());
        CHECK(mockManager->droppedIds.empty());

        std::filesystem::remove(tempFile);
    }

    SECTION("Shared BufferPool is owned by IBackend and providesOwnMemory() skips strategy scratch buffers")
    {
        Backend::Vendor::VendorBackend defaultBackend(mockManager);
        REQUIRE(defaultBackend.bufferPool() != nullptr);
        CHECK(defaultBackend.bufferPool() == defaultBackend.bufferPool());

        Backend::Vendor::VendorInputChannel inputChannel("vendor_in", defaultBackend, mockReceiver);
        CHECK(inputChannel.providesOwnMemory());

        // Verify that strategies pass empty slot.buffer when providesOwnMemory() is true
        class BufferInspectInputChannel : public Backend::Vendor::VendorInputChannel {
        public:
            using Backend::Vendor::VendorInputChannel::VendorInputChannel;

            Common::Net::BytesResult receiveSlot(Backend::BufferSlot& slot, Timeout timeout) override
            {
                sawNonEmptyTargetBuffer = sawNonEmptyTargetBuffer || !slot.buffer.empty();
                return Backend::Vendor::VendorInputChannel::receiveSlot(slot, timeout);
            }

            std::atomic<bool> sawNonEmptyTargetBuffer { false };
        };

        auto inspectChannel = std::make_unique<BufferInspectInputChannel>("vendor_in", defaultBackend, mockReceiver);
        auto* inspectPtr = inspectChannel.get();

        VendorTestHandler handler(defaultBackend, /*shouldDrop=*/false);
        handler.registerInputChannel(std::move(inspectChannel));

        mockReceiver->enqueue(9501, "Zero-Scratch Msg 1");
        mockReceiver->enqueue(9502, "Zero-Scratch Msg 2");

        Core::Handler::HandlerRunner runner(handler);
        runner.setExecutionStrategy(std::make_unique<Core::Threading::ThreadPoolStrategy>(2, /*strictFifo=*/false, true));
        runner.startAsync(false);

        auto start = std::chrono::steady_clock::now();
        while (mockManager->finalizedCount() < 2 && std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        runner.stop();
        runner.wait();

        CHECK_FALSE(inspectPtr->sawNonEmptyTargetBuffer.load());
        CHECK(handler.receivedCount() == 2);
    }

    SECTION("Mode B missing payload file drops VendorMessage and returns ReceiveFailure")
    {
        Backend::Vendor::VendorInputChannel inputChannel("vendor_in", backend, mockReceiver);
        mockReceiver->enqueueFile(9003, "/nonexistent/path/vendor_missing_payload_9003.dat");

        Backend::BufferSlot slot { };
        auto res = inputChannel.receiveSlot(slot, std::chrono::milliseconds(10));
        REQUIRE_FALSE(res.hasValue());
        CHECK_FALSE(slot.lease.active());
        REQUIRE(mockManager->droppedIds.size() == 1);
        CHECK(mockManager->droppedIds.front() == 9003);
        CHECK(mockManager->finalizedIds.empty());
    }

    SECTION("Foreign SettlementToken is not downcast to VendorRelayNode and falls back to payload copy")
    {
        Backend::Vendor::VendorOutputChannel outputChannel("vendor_out", mockManager, mockSender);

        int customContext = 42;
        bool customSettled = false;
        struct ForeignState {
            int* value;
            bool* settled;
        } foreignState { &customContext, &customSettled };

        Backend::SettlementToken foreignToken(
            &foreignState,
            [](void* ctx, Backend::Settlement /*outcome*/) noexcept {
                auto* s = static_cast<ForeignState*>(ctx);
                *s->settled = true;
            });

        Backend::Lease lease(std::move(foreignToken));
        const std::string payload = "Foreign Token Payload";
        Backend::MessageView view { Common::Memory::ByteView(payload) };
        view.attachLease(lease);

        REQUIRE(outputChannel.send(view));
        REQUIRE(mockSender->sentMessages.size() == 1);
        CHECK(mockSender->sentMessages.front().getId() == 0);
        CHECK(mockSender->sentPayloads.front() == payload);

        // Foreign lease must remain active and settle via its own callback
        REQUIRE(lease.active());
        lease.ack();
        CHECK(customSettled);
    }
}
