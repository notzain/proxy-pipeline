#include "core/DedicatedThreadStrategy.hpp"
#include "core/IHandler.hpp"
#include "core/SequentialStrategy.hpp"
#include "core/ThreadPoolStrategy.hpp"
#include <catch2/catch_test_macros.hpp>
#include <fmt/core.h>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace {

class MockInputChannel : public Backend::IInputChannel {
public:
    explicit MockInputChannel(std::vector<std::string> messages)
        : m_messages(std::move(messages))
    {
    }

    std::optional<Buffer> receive(Timeout) override
    {
        std::lock_guard lock(m_mutex);
        if (m_index < m_messages.size()) {
            const auto& msg = m_messages[m_index++];
            Buffer buf(msg.size());
            std::memcpy(buf.data(), msg.data(), msg.size());
            return buf;
        }
        return std::nullopt;
    }

    std::size_t receiveInto(std::span<std::byte> destination, Timeout) override
    {
        std::lock_guard lock(m_mutex);
        if (m_index < m_messages.size()) {
            const auto& msg = m_messages[m_index++];
            const auto bytesToCopy = std::min(destination.size(), msg.size());
            std::memcpy(destination.data(), msg.data(), bytesToCopy);
            return bytesToCopy;
        }
        return 0;
    }

    std::size_t receiveBatch(
        std::span<const std::span<std::byte>> destinations,
        std::span<std::size_t> outSizes,
        Timeout) override
    {
        std::lock_guard lock(m_mutex);
        if (destinations.empty() || outSizes.size() < destinations.size()) {
            return 0;
        }
        std::size_t count = 0;
        while (count < destinations.size() && m_index < m_messages.size()) {
            const auto& msg = m_messages[m_index];
            // If the message exceeds the destination buffer slot, do not consume it in batch:
            // leave it for full-MTU fallback receiveInto, or if it's the first message, signal saturation
            if (msg.size() > destinations[count].size()) {
                // Peek/truncate into buffer to signal saturation without popping from queue
                std::memcpy(destinations[count].data(), msg.data(), destinations[count].size());
                outSizes[count] = destinations[count].size();
                // Do not pop m_index: fallback receiveInto will read the full message
                ++count;
                break;
            }
            std::memcpy(destinations[count].data(), msg.data(), msg.size());
            outSizes[count] = msg.size();
            m_index++;
            ++count;
        }
        return count;
    }

private:
    std::mutex m_mutex;
    std::vector<std::string> m_messages;
    std::size_t m_index { 0 };
};

class MockOutputChannel : public Backend::IOutputChannel {
public:
    bool send(std::span<const std::byte> message) override
    {
        std::lock_guard lock(m_mutex);
        std::string str(reinterpret_cast<const char*>(message.data()), message.size());
        sentMessages.push_back(str);
        return true;
    }

    std::mutex m_mutex;
    std::vector<std::string> sentMessages;
};

class TestChainedHandler : public Core::IHandler {
public:
    void onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer) override
    {
        std::lock_guard lock(m_handlerMutex);
        std::string text(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        receivedMessages.push_back(fmt::format("{}:{}", channelName, text));

        if (hasOutputChannel("out")) {
            send("out", buffer);
        }

        m_count++;
        if (m_count >= 2) {
            stop();
        }
    }

    std::mutex m_handlerMutex;
    std::vector<std::string> receivedMessages;

private:
    int m_count { 0 };
};

} // namespace

TEST_CASE("IHandler output channel registration and dispatch", "[channel][core]")
{
    TestChainedHandler handler;
    auto mockChan = std::make_unique<MockOutputChannel>();
    auto* rawChan = mockChan.get();

    handler.registerOutputChannel("out_1", std::move(mockChan));
    CHECK(handler.hasOutputChannel("out_1"));
    CHECK_FALSE(handler.hasOutputChannel("out_2"));

    std::string msg = "Hello Sender";
    std::span<const std::byte> byteSpan(reinterpret_cast<const std::byte*>(msg.data()), msg.size());

    CHECK(handler.send("out_1", byteSpan));
    CHECK_FALSE(handler.send("out_2", byteSpan));

    REQUIRE(rawChan->sentMessages.size() == 1);
    CHECK(rawChan->sentMessages[0] == "Hello Sender");
}

TEST_CASE("IHandler Sequential execution model", "[channel][core]")
{
    TestChainedHandler handler;
    handler.setExecutionModel(Core::ExecutionModel::Sequential);

    auto inChan1 = std::make_unique<MockInputChannel>(std::vector<std::string> { "msgA" });
    auto inChan2 = std::make_unique<MockInputChannel>(std::vector<std::string> { "msgB" });
    auto outChan = std::make_unique<MockOutputChannel>();
    auto* rawOutChan = outChan.get();

    handler.registerInputChannel("chan1", std::move(inChan1));
    handler.registerInputChannel("chan2", std::move(inChan2));
    handler.registerOutputChannel("out", std::move(outChan));

    handler.start();

    REQUIRE(handler.receivedMessages.size() == 2);
    CHECK(handler.receivedMessages[0] == "chan1:msgA");
    CHECK(handler.receivedMessages[1] == "chan2:msgB");

    REQUIRE(rawOutChan->sentMessages.size() == 2);
    CHECK(rawOutChan->sentMessages[0] == "msgA");
    CHECK(rawOutChan->sentMessages[1] == "msgB");
}

TEST_CASE("IHandler Dedicated thread execution model with async start", "[channel][core]")
{
    TestChainedHandler handler;
    handler.setExecutionModel(Core::ExecutionModel::Dedicated);

    auto inChan1 = std::make_unique<MockInputChannel>(std::vector<std::string> { "msgA" });
    auto inChan2 = std::make_unique<MockInputChannel>(std::vector<std::string> { "msgB" });
    auto outChan = std::make_unique<MockOutputChannel>();
    auto* rawOutChan = outChan.get();

    handler.registerInputChannel("chan1", std::move(inChan1));
    handler.registerInputChannel("chan2", std::move(inChan2));
    handler.registerOutputChannel("out", std::move(outChan));

    handler.startAsync();
    handler.wait();

    REQUIRE(handler.receivedMessages.size() == 2);
    REQUIRE(rawOutChan->sentMessages.size() == 2);
}

TEST_CASE("IHandler ThreadPool execution model", "[channel][core]")
{
    TestChainedHandler handler;
    handler.setExecutionModel(Core::ExecutionModel::ThreadPool, 2);

    auto inChan1 = std::make_unique<MockInputChannel>(std::vector<std::string> { "msgA" });
    auto inChan2 = std::make_unique<MockInputChannel>(std::vector<std::string> { "msgB" });
    auto outChan = std::make_unique<MockOutputChannel>();
    auto* rawOutChan = outChan.get();

    handler.registerInputChannel("chan1", std::move(inChan1));
    handler.registerInputChannel("chan2", std::move(inChan2));
    handler.registerOutputChannel("out", std::move(outChan));

    handler.startAsync();
    handler.wait();

    REQUIRE(handler.receivedMessages.size() == 2);
    REQUIRE(rawOutChan->sentMessages.size() == 2);
}

TEST_CASE("IHandler sendAll broadcasts to all registered channels", "[channel][core]")
{
    TestChainedHandler handler;
    auto chanA = std::make_unique<MockOutputChannel>();
    auto chanB = std::make_unique<MockOutputChannel>();
    auto* rawA = chanA.get();
    auto* rawB = chanB.get();

    handler.registerOutputChannel("out_a", std::move(chanA));
    handler.registerOutputChannel("out_b", std::move(chanB));

    std::string msg = "Broadcast Message";
    std::span<const std::byte> byteSpan(reinterpret_cast<const std::byte*>(msg.data()), msg.size());

    CHECK(handler.sendAll(byteSpan) == 2);
    REQUIRE(rawA->sentMessages.size() == 1);
    CHECK(rawA->sentMessages[0] == "Broadcast Message");
    REQUIRE(rawB->sentMessages.size() == 1);
    CHECK(rawB->sentMessages[0] == "Broadcast Message");
}

TEST_CASE("MergeHandler redirects multiple input channels to single output channel", "[channel][core]")
{
    class MockMergeHandler : public Core::IHandler {
    public:
        explicit MockMergeHandler(std::string outChannel)
            : m_outChannel(std::move(outChannel))
        {
        }

        void onMessageReceived(std::string_view, std::span<const std::byte> buffer) override
        {
            send(m_outChannel, buffer);
            m_count++;
            if (m_count >= 2) {
                stop();
            }
        }

    private:
        std::string m_outChannel;
        std::atomic<int> m_count { 0 };
    };

    MockMergeHandler merge("merged_out");
    auto inA = std::make_unique<MockInputChannel>(std::vector<std::string> { "input1" });
    auto inB = std::make_unique<MockInputChannel>(std::vector<std::string> { "input2" });
    auto outChan = std::make_unique<MockOutputChannel>();
    auto* rawOut = outChan.get();

    merge.registerInputChannel("in_a", std::move(inA));
    merge.registerInputChannel("in_b", std::move(inB));
    merge.registerOutputChannel("merged_out", std::move(outChan));

    merge.start();

    REQUIRE(rawOut->sentMessages.size() == 2);
}

#include "backend/uds/UdsInputChannel.hpp"
#include "backend/uds/UdsOutputChannel.hpp"

TEST_CASE("UDS Input and Output channels send and receive messages", "[channel][uds]")
{
    const std::string sockPath = "/tmp/test_proxy_channel.sock";
    Proxy::Backend::Uds::UdsInputChannel inChannel(sockPath);
    Proxy::Backend::Uds::UdsOutputChannel outChannel(sockPath);

    std::string testMsg = "Hello UDS IPC!";
    std::span<const std::byte> sendSpan(reinterpret_cast<const std::byte*>(testMsg.data()), testMsg.size());

    REQUIRE(outChannel.send(sendSpan));

    std::array<std::byte, 128> recvBuf { };
    std::size_t received = inChannel.receiveInto(recvBuf, std::chrono::milliseconds(500));
    REQUIRE(received == testMsg.size());

    std::string receivedStr(reinterpret_cast<const char*>(recvBuf.data()), received);
    CHECK(receivedStr == testMsg);
}

TEST_CASE("UDS Input channel drains multiple packets via receiveBatch", "[channel][uds][batch]")
{
    const std::string sockPath = "/tmp/test_proxy_uds_batch.sock";
    Proxy::Backend::Uds::UdsInputChannel inChannel(sockPath);
    Proxy::Backend::Uds::UdsOutputChannel outChannel(sockPath);

    constexpr std::size_t numMessages = 5;
    std::vector<std::string> sentMessages;
    for (std::size_t i = 0; i < numMessages; ++i) {
        std::string msg = fmt::format("Batch Message #{}", i);
        sentMessages.push_back(msg);
        std::span<const std::byte> sendSpan(reinterpret_cast<const std::byte*>(msg.data()), msg.size());
        REQUIRE(outChannel.send(sendSpan));
    }

    std::array<std::array<std::byte, 256>, numMessages> buffers;
    std::array<std::span<std::byte>, numMessages> destSpans;
    for (std::size_t i = 0; i < numMessages; ++i) {
        destSpans[i] = std::span<std::byte>(buffers[i].data(), buffers[i].size());
    }
    std::array<std::size_t, numMessages> outSizes { };

    std::size_t drained = inChannel.receiveBatch(
        std::span<const std::span<std::byte>>(destSpans.data(), destSpans.size()),
        outSizes,
        std::chrono::milliseconds(500));

    REQUIRE(drained == numMessages);
    for (std::size_t i = 0; i < numMessages; ++i) {
        REQUIRE(outSizes[i] == sentMessages[i].size());
        std::string receivedStr(reinterpret_cast<const char*>(buffers[i].data()), outSizes[i]);
        CHECK(receivedStr == sentMessages[i]);
    }
}

TEST_CASE("Execution models handle oversized jumbo messages exceeding 2048 bytes", "[channel][core][jumbo]")
{
    class JumboVerifyHandler : public Core::IHandler {
    public:
        void onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer) override
        {
            std::lock_guard lock(m_mutex);
            lastReceivedChannel = std::string(channelName);
            lastReceivedSize = buffer.size();
            lastReceivedPayload = std::string(reinterpret_cast<const char*>(buffer.data()), buffer.size());
            m_count++;
            if (buffer.size() > 2048) {
                stop();
            }
        }

        std::mutex m_mutex;
        std::string lastReceivedChannel;
        std::size_t lastReceivedSize { 0 };
        std::string lastReceivedPayload;

    private:
        int m_count { 0 };
    };

    SECTION("ThreadPool execution model receives oversized 4096-byte packet")
    {
        JumboVerifyHandler handler;
        handler.setExecutionModel(Core::ExecutionModel::ThreadPool, 2);

        // Generate a 4096-byte payload (exceeds 2048-byte default batch slot)
        std::string jumboPayload(4096, 'X');
        auto inChan = std::make_unique<MockInputChannel>(std::vector<std::string> { jumboPayload });

        handler.registerInputChannel("jumbo_in", std::move(inChan));
        handler.startAsync();
        handler.wait();

        REQUIRE(handler.lastReceivedChannel == "jumbo_in");
        REQUIRE(handler.lastReceivedSize == 4096);
        CHECK(handler.lastReceivedPayload == jumboPayload);
    }

    SECTION("Dedicated thread model receives oversized 8192-byte packet")
    {
        JumboVerifyHandler handler;
        handler.setExecutionModel(Core::ExecutionModel::Dedicated);

        std::string jumboPayload(8192, 'Y');
        auto inChan = std::make_unique<MockInputChannel>(std::vector<std::string> { jumboPayload });

        handler.registerInputChannel("jumbo_in", std::move(inChan));
        handler.startAsync();
        handler.wait();

        REQUIRE(handler.lastReceivedChannel == "jumbo_in");
        REQUIRE(handler.lastReceivedSize == 8192);
        CHECK(handler.lastReceivedPayload == jumboPayload);
    }

    SECTION("Sequential model receives oversized 4096-byte packet")
    {
        JumboVerifyHandler handler;
        handler.setExecutionModel(Core::ExecutionModel::Sequential);

        std::string jumboPayload(4096, 'Z');
        auto inChan = std::make_unique<MockInputChannel>(std::vector<std::string> { jumboPayload });

        handler.registerInputChannel("jumbo_in", std::move(inChan));
        handler.startAsync();
        handler.wait();

        REQUIRE(handler.lastReceivedChannel == "jumbo_in");
        REQUIRE(handler.lastReceivedSize == 4096);
        CHECK(handler.lastReceivedPayload == jumboPayload);
    }
}
