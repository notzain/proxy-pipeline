#include "core/IHandler.hpp"
#include "core/IMessageReceiver.hpp"
#include "core/IMessageSender.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <fmt/core.h>
#include <memory>
#include <string>
#include <vector>

namespace {

class MockInputChannel : public Backend::IInputChannel {
public:
    explicit MockInputChannel(std::vector<std::string> messages)
        : m_messages(std::move(messages))
    {
    }

    std::optional<Buffer> receive(Timeout /*timeout*/) override
    {
        if (m_index < m_messages.size()) {
            const auto& str = m_messages[m_index++];
            Buffer buf(str.size());
            std::memcpy(buf.data(), str.data(), str.size());
            return buf;
        }
        return std::nullopt;
    }

private:
    std::vector<std::string> m_messages;
    std::size_t m_index { 0 };
};

class MockOutputChannel : public Backend::IOutputChannel {
public:
    bool send(std::span<const std::byte> message) override
    {
        std::string str(reinterpret_cast<const char*>(message.data()), message.size());
        sentMessages.push_back(str);
        return true;
    }

    std::vector<std::string> sentMessages;
};

class TestChainedHandler : public Core::IHandler, public Core::IMessageReceiver, public Core::IMessageSender {
public:
    void onMessageReceived(std::string_view channelName, std::span<const std::byte> buffer) override
    {
        std::string text(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        receivedMessages.push_back(fmt::format("{}:{}", channelName, text));

        // Forward to out channel if registered
        if (hasOutputChannel("out")) {
            send("out", buffer);
        }

        m_count++;
        if (m_count >= 2) {
            stop();
        }
    }

    std::vector<std::string> receivedMessages;

private:
    int m_count { 0 };
};

} // namespace

TEST_CASE("IMessageSender channel registration and dispatch", "[channel][core]")
{
    Core::IMessageSender sender;
    auto mockChan = std::make_unique<MockOutputChannel>();
    auto* rawChan = mockChan.get();

    sender.registerOutputChannel("out_1", std::move(mockChan));
    CHECK(sender.hasOutputChannel("out_1"));
    CHECK_FALSE(sender.hasOutputChannel("out_2"));

    std::string msg = "Hello Sender";
    std::span<const std::byte> byteSpan(reinterpret_cast<const std::byte*>(msg.data()), msg.size());

    CHECK(sender.send("out_1", byteSpan));
    CHECK_FALSE(sender.send("out_2", byteSpan));

    REQUIRE(rawChan->sentMessages.size() == 1);
    CHECK(rawChan->sentMessages[0] == "Hello Sender");
}

TEST_CASE("IHandler multi-channel event loop with IMessageReceiver and IMessageSender chaining", "[channel][core]")
{
    TestChainedHandler handler;

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

TEST_CASE("IMessageSender sendAll broadcasts to all registered channels", "[channel][core]")
{
    Core::IMessageSender sender;
    auto chanA = std::make_unique<MockOutputChannel>();
    auto chanB = std::make_unique<MockOutputChannel>();
    auto* rawA = chanA.get();
    auto* rawB = chanB.get();

    sender.registerOutputChannel("out_a", std::move(chanA));
    sender.registerOutputChannel("out_b", std::move(chanB));

    std::string msg = "Broadcast Message";
    std::span<const std::byte> byteSpan(reinterpret_cast<const std::byte*>(msg.data()), msg.size());

    CHECK(sender.sendAll(byteSpan) == 2);
    REQUIRE(rawA->sentMessages.size() == 1);
    CHECK(rawA->sentMessages[0] == "Broadcast Message");
    REQUIRE(rawB->sentMessages.size() == 1);
    CHECK(rawB->sentMessages[0] == "Broadcast Message");
}
