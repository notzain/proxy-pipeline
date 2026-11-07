#include "UdpEgressHandler.hpp"
#include "UdpIngressHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/channel/IInputChannel.hpp"
#include "backend/channel/IOutputChannel.hpp"
#include "backend/message/EnvelopeHeader.hpp"
#include "backend/message/MessageView.hpp"
#include "common/memory/ByteView.hpp"
#include "core/config/Configuration.hpp"
#include "core/handler/HandlerRunner.hpp"
#include "core/net/UdpInputChannel.hpp"
#include "core/net/UdpMetadata.hpp"
#include "core/net/UdpOutputChannel.hpp"
#include <arpa/inet.h>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <span>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using Common::Net::BatchResult;
using Common::Net::BytesResult;

void sendTestUdpDatagram(const std::string& host, std::uint16_t port, std::string_view payload)
{
    int clientFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    REQUIRE(clientFd >= 0);

    sockaddr_in destAddr { };
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(port);
    ::inet_pton(AF_INET, host.c_str(), &destAddr.sin_addr);

    ssize_t sent = ::sendto(
        clientFd,
        payload.data(),
        payload.size(),
        0,
        reinterpret_cast<const sockaddr*>(&destAddr),
        sizeof(destAddr));

    REQUIRE(sent == static_cast<ssize_t>(payload.size()));
    ::close(clientFd);
}

class TestOutputChannel : public ::Backend::IOutputChannel {
public:
    using ::Backend::IOutputChannel::send;
    using ::Backend::IOutputChannel::sendBatch;

    bool send(Common::Compat::span<const std::byte> message) override
    {
        std::lock_guard lock(m_mutex);
        std::string str(reinterpret_cast<const char*>(message.data()), message.size());
        m_sentMessages.push_back(std::move(str));
        return true;
    }

    std::size_t sendBatch(Common::Compat::span<const Common::Compat::span<const std::byte>> messages) override
    {
        std::lock_guard lock(m_mutex);
        for (const auto& span : messages) {
            std::string str(reinterpret_cast<const char*>(span.data()), span.size());
            m_sentMessages.push_back(std::move(str));
        }
        return messages.size();
    }

    std::vector<std::string> getSentMessages() const
    {
        std::lock_guard lock(m_mutex);
        return m_sentMessages;
    }

    void clear()
    {
        std::lock_guard lock(m_mutex);
        m_sentMessages.clear();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_sentMessages;
};

class MockTestInputChannel : public ::Backend::IInputChannel {
public:
    explicit MockTestInputChannel(std::vector<std::vector<std::byte>> messages)
        : m_messages(std::move(messages))
    {
    }

    BytesResult receiveSlot(::Backend::BufferSlot& slot, Timeout) override
    {
        slot.size = 0;
        if (m_index < m_messages.size()) {
            const auto& msg = m_messages[m_index++];
            std::size_t count = std::min(slot.buffer.size(), msg.size());
            std::memcpy(slot.buffer.data(), msg.data(), count);
            slot.size = count;
            return BytesResult::ok(count);
        }
        return BytesResult::err(Common::Net::ReceiveTimeout { });
    }

    BatchResult receiveBatch(
        Common::Compat::span<::Backend::BufferSlot> slots,
        Timeout timeout) override
    {
        std::size_t count = 0;
        while (count < slots.size()) {
            BytesResult result = receiveSlot(slots[count], timeout);
            if (result.isOk()) {
                count++;
            } else if (result.unwrapErr().is<Common::Net::ReceiveTimeout>()) {
                break;
            } else {
                return BatchResult::err({ count, result.unwrapErr() });
            }
        }
        return count > 0
            ? BatchResult::ok(count)
            : BatchResult::err({ 0, Common::Net::ReceiveTimeout { } });
    }

private:
    std::vector<std::vector<std::byte>> m_messages;
    std::size_t m_index { 0 };
};

class DummyTestBackend : public ::Backend::IBackend {
public:
    void configure(const ::Backend::IConfig&) override { }

    std::unique_ptr<::Backend::IInputChannel> createInputChannel(std::string_view, const ::Backend::IConfig&) override
    {
        if (!m_mockInputs.empty()) {
            auto ch = std::move(m_mockInputs.front());
            m_mockInputs.erase(m_mockInputs.begin());
            return ch;
        }
        return nullptr;
    }

    std::unique_ptr<::Backend::IOutputChannel> createOutputChannel(std::string_view, const ::Backend::IConfig&) override
    {
        auto channel = std::make_unique<TestOutputChannel>();
        m_lastCreatedOutput = channel.get();
        return channel;
    }

    void queueInputChannel(std::unique_ptr<::Backend::IInputChannel> ch)
    {
        m_mockInputs.push_back(std::move(ch));
    }

    TestOutputChannel* m_lastCreatedOutput { nullptr };
    std::vector<std::unique_ptr<::Backend::IInputChannel>> m_mockInputs;
};

} // namespace

TEST_CASE("Backend::MessageView binary wire packing and parsing", "[message][envelope]")
{
    std::string testPayload = "<order><item>Widget</item></order>";
    Common::Memory::ByteView payloadSpan(testPayload);

    SECTION("Pack raw envelope")
    {
        auto env = Backend::MessageView::packRaw(payloadSpan).value();
        auto view = Backend::MessageView::from(env);

        REQUIRE(view.has_value());
        CHECK(view->protocol() == Backend::RawProtocol);
        CHECK(view->metadata().empty());
        CHECK(view->payload().size() == testPayload.size());

        std::string_view parsedPayload(reinterpret_cast<const char*>(view->payload().data()), view->payload().size());
        CHECK(parsedPayload == testPayload);
    }

    SECTION("Pack UDP envelope with metadata")
    {
        Core::Net::UdpMetadata meta { };
        meta.srcIp = 0x0100007F; // 127.0.0.1
        meta.srcPort = htons(54321);
        meta.dstIp = 0x0100007F;
        meta.dstPort = htons(9123);

        auto env = Backend::MessageView::pack(meta, payloadSpan).value();
        auto view = Backend::MessageView::from(env);

        REQUIRE(view.has_value());
        CHECK(view->protocol() == Backend::MetadataTraits<Core::Net::UdpMetadata>::protocol);

        const auto parsedMeta = view->getMetadata<Core::Net::UdpMetadata>();
        REQUIRE(parsedMeta.has_value());
        CHECK(parsedMeta->srcPort == htons(54321));
        CHECK(parsedMeta->dstPort == htons(9123));

        std::string_view parsedPayload(reinterpret_cast<const char*>(view->payload().data()), view->payload().size());
        CHECK(parsedPayload == testPayload);
    }

    SECTION("Mutate payload using withPayload")
    {
        Core::Net::UdpMetadata meta { };
        meta.srcPort = htons(1122);
        auto env = Backend::MessageView::pack(meta, payloadSpan).value();
        auto view = Backend::MessageView::from(env);
        REQUIRE(view.has_value());

        std::string updatedPayload = "<order><item>SuperWidget</item><qty>10</qty></order>";
        Common::Memory::ByteView updatedSpan(updatedPayload);

        auto mutatedEnv = Backend::MessageView::withPayload(*view, updatedSpan).value();
        auto mutatedView = Backend::MessageView::from(mutatedEnv);

        REQUIRE(mutatedView.has_value());
        CHECK(mutatedView->protocol() == Backend::MetadataTraits<Core::Net::UdpMetadata>::protocol);
        const auto mutatedMeta = mutatedView->getMetadata<Core::Net::UdpMetadata>();
        REQUIRE(mutatedMeta.has_value());
        CHECK(mutatedMeta->srcPort == htons(1122));

        std::string_view parsedMutated(reinterpret_cast<const char*>(mutatedView->payload().data()), mutatedView->payload().size());
        CHECK(parsedMutated == updatedPayload);

        // Non-allocating member view.withPayload(newPayload) preserves metadata and packs on IOutputChannel::send()
        auto reboundView = view->withPayload(updatedSpan);
        CHECK_FALSE(reboundView.hasWireEnvelope());
        CHECK(reboundView.protocol() == Backend::MetadataTraits<Core::Net::UdpMetadata>::protocol);
        const auto reboundMeta = reboundView.getMetadata<Core::Net::UdpMetadata>();
        REQUIRE(reboundMeta.has_value());
        CHECK(reboundMeta->srcPort == htons(1122));

        TestOutputChannel outChan;
        REQUIRE(outChan.send(reboundView));
        const auto sentMsgs = outChan.getSentMessages();
        REQUIRE(sentMsgs.size() == 1);
        auto sentView = Backend::MessageView::from(Common::Memory::ByteView(sentMsgs.front()));
        REQUIRE(sentView.has_value());
        CHECK(sentView->getMetadata<Core::Net::UdpMetadata>()->srcPort == htons(1122));
        std::string_view sentPayload(reinterpret_cast<const char*>(sentView->payload().data()), sentView->payload().size());
        CHECK(sentPayload == updatedPayload);
    }

    SECTION("Explicit wire format encoding and decoding with fixed byte sequence")
    {
        // 18 bytes header:
        // magic: 0x50, 0x58
        // version: 1
        // protocol: 0 (Raw)
        // metaLen: 0x00, 0x00
        // payloadLen: 0x00, 0x00, 0x00, 0x05 (5 bytes)
        // timestampNs: 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
        // payload: 'H', 'e', 'l', 'l', 'o'
        const std::array<std::uint8_t, 23> goldenWire = {
            0x50, 0x58, // Magic
            0x01, // Version
            0x00, // RawProtocol
            0x00, 0x00, // Metadata length = 0
            0x00, 0x00, 0x00, 0x05, // Payload length = 5
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, // Timestamp
            'H', 'e', 'l', 'l', 'o' // Payload
        };

        auto wireSpan = Common::Compat::as_bytes(Common::Compat::span<const std::uint8_t, 23>(goldenWire));
        auto view = Backend::MessageView::from(wireSpan);
        REQUIRE(view.has_value());
        CHECK(view->protocol() == Backend::RawProtocol);
        CHECK(view->metadata().empty());
        CHECK(view->payload().size() == 5);
        CHECK(view->timestampNs() == 0x0102030405060708ULL);
        std::string_view payloadStr(reinterpret_cast<const char*>(view->payload().data()), view->payload().size());
        CHECK(payloadStr == "Hello");

        // Verify packing produces identical bytes
        std::vector<std::byte> packed = Backend::MessageView::packRaw(
            view->payload(),
            0x0102030405060708ULL)
                                            .value();
        REQUIRE(packed.size() == goldenWire.size());
        for (std::size_t i = 0; i < packed.size(); ++i) {
            CHECK(static_cast<std::uint8_t>(packed[i]) == goldenWire[i]);
        }
    }

    SECTION("Custom backend protocol view exposes payload uniformly")
    {
        // Simulate a custom backend frame that doesn't use 0x5058 header
        std::string customPayload = "custom_backend_message_data";
        Common::Memory::ByteView payload(customPayload);

        // Custom backend creates a MessageView directly wrapping its payload and raw frame
        Backend::MessageView customView(payload);
        CHECK(customView.payload().size() == customPayload.size());
        CHECK(customView.rawBuffer().size() == customPayload.size());
        std::string_view extracted(reinterpret_cast<const char*>(customView.payload().data()), customView.payload().size());
        CHECK(extracted == customPayload);
    }
}

TEST_CASE("Core::Net::UdpInputChannel receives external UDP datagrams as envelopes", "[net][udp]")
{
    constexpr std::uint16_t testPort = 19876;
    Core::Net::UdpInputChannel channel("127.0.0.1", testPort);

    REQUIRE(channel.getNativeSocket() >= 0);
    REQUIRE(channel.getHost() == "127.0.0.1");
    REQUIRE(channel.getPort() == testPort);

    SECTION("Receive single datagram into preallocated buffer")
    {
        std::string expectedPayload = "<order><customer>Bob</customer><item>Gizmo</item><quantity>5</quantity></order>";
        sendTestUdpDatagram("127.0.0.1", testPort, expectedPayload);

        std::array<std::byte, 2048> buffer;
        Backend::BufferSlot slot { Common::Compat::span<std::byte>(buffer), 0 };
        auto receiveResult = channel.receiveSlot(slot, std::chrono::milliseconds(500));

        REQUIRE(receiveResult.isOk());
        REQUIRE(receiveResult.unwrap() > expectedPayload.size());
        auto view = Backend::MessageView::from(slot.filledSpan());
        REQUIRE(view.has_value());
        CHECK(view->protocol() == Backend::MetadataTraits<Core::Net::UdpMetadata>::protocol);

        const auto meta = view->getMetadata<Core::Net::UdpMetadata>();
        REQUIRE(meta.has_value());
        CHECK(ntohs(meta->dstPort) == testPort);

        std::string_view receivedView(reinterpret_cast<const char*>(view->payload().data()), view->payload().size());
        CHECK(receivedView == expectedPayload);
    }
}

TEST_CASE("Core::Net::UdpInputChannel rejects truncated datagrams", "[net][udp][hardening]")
{
    constexpr std::uint16_t testPort = 19877;
    Core::Net::UdpInputChannel channel("127.0.0.1", testPort);
    REQUIRE(channel.getNativeSocket() >= 0);

    const std::string oversizedPayload(100, 'x');
    sendTestUdpDatagram("127.0.0.1", testPort, oversizedPayload);

    std::array<std::byte, 80> buffer { };
    Backend::BufferSlot slot { buffer, 0 };
    auto result = channel.receiveSlot(slot, std::chrono::milliseconds(500));

    REQUIRE_FALSE(result.isOk());
    const auto* failure = std::get_if<Common::Net::ReceiveFailure>(&result.unwrapErr());
    REQUIRE(failure != nullptr);
    CHECK(failure->code == std::make_error_code(std::errc::message_size));
    CHECK(slot.size == 0);

    sendTestUdpDatagram("127.0.0.1", testPort, oversizedPayload);
    std::array<Backend::BufferSlot, 1> slots {
        Backend::BufferSlot { Common::Compat::span<std::byte>(buffer), 0 }
    };
    auto batchResult = channel.receiveBatch(slots, std::chrono::milliseconds(500));
    REQUIRE_FALSE(batchResult.isOk());
    CHECK(slots[0].size == 0);
}

TEST_CASE("UDP channel moves preserve their names", "[net][udp][move]")
{
    SECTION("input channel move construction and assignment")
    {
        Core::Net::UdpInputChannel source("input_source", "127.0.0.1", 0);
        Core::Net::UdpInputChannel moved(std::move(source));
        CHECK(moved.name() == "input_source");

        Core::Net::UdpInputChannel destination("input_destination", "127.0.0.1", 0);
        destination = std::move(moved);
        CHECK(destination.name() == "input_source");
    }

    SECTION("output channel move construction and assignment")
    {
        Core::Net::UdpOutputChannel source("output_source");
        Core::Net::UdpOutputChannel moved(std::move(source));
        CHECK(moved.name() == "output_source");

        Core::Net::UdpOutputChannel destination("output_destination");
        destination = std::move(moved);
        CHECK(destination.name() == "output_source");
    }
}

TEST_CASE("UdpIngressHandler packs datagram into envelope and forwards to pipeline backend", "[handler][ingress]")
{
    constexpr std::uint16_t testPort = 19877;

    Core::Config::Configuration config;
    config.set("ingress.host", "127.0.0.1");
    config.set("ingress.port", std::to_string(testPort));
    config.set("ingress.output_channel", "pipeline_ingress_queue");

    DummyTestBackend backend;
    Proxy::Handler::UdpIngressHandler handler(backend);
    Core::Handler::HandlerRunner runner(handler);

    REQUIRE(runner.configure(config));
    REQUIRE(backend.m_lastCreatedOutput != nullptr);

    SECTION("Single datagram received and forwarded as valid envelope")
    {
        std::string testOrder = "<order><customer>Alice</customer><item>Widget</item><quantity>42</quantity></order>";
        sendTestUdpDatagram("127.0.0.1", testPort, testOrder);

        runner.startAsync();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        runner.stop();
        runner.wait();

        const auto& sentMessages = backend.m_lastCreatedOutput->getSentMessages();
        REQUIRE(sentMessages.size() >= 1);

        auto view = Backend::MessageView::from(Common::Memory::ByteView(sentMessages.front()));

        REQUIRE(view.has_value());
        CHECK(view->protocol() == Backend::MetadataTraits<Core::Net::UdpMetadata>::protocol);
        std::string_view payloadStr(reinterpret_cast<const char*>(view->payload().data()), view->payload().size());
        CHECK(payloadStr == testOrder);
    }
}

TEST_CASE("UdpEgressHandler restores client metadata and transmits reply over UDP", "[handler][egress]")
{
    // Create an ephemeral receiving UDP socket representing the external client
    int clientFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    REQUIRE(clientFd >= 0);

    sockaddr_in clientAddr { };
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    clientAddr.sin_port = 0; // OS binds to ephemeral port
    REQUIRE(::bind(clientFd, reinterpret_cast<sockaddr*>(&clientAddr), sizeof(clientAddr)) == 0);

    socklen_t addrLen = sizeof(clientAddr);
    REQUIRE(::getsockname(clientFd, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen) == 0);
    std::uint16_t clientPort = ntohs(clientAddr.sin_port);

    // Pack an envelope addressed to this client
    Core::Net::UdpMetadata meta { };
    meta.srcIp = htonl(INADDR_LOOPBACK);
    meta.srcPort = clientAddr.sin_port;
    meta.dstPort = htons(9999);

    std::string expectedPayload = "<order_receipt status=\"confirmed\"/>";
    auto envelope = Backend::MessageView::pack(
        meta,
        Common::Memory::ByteView(expectedPayload))
                        .value();

    // Configure Egress Handler reading from a mock input channel
    DummyTestBackend backend;
    std::vector<std::vector<std::byte>> queuedMsgs { envelope };
    backend.queueInputChannel(std::make_unique<MockTestInputChannel>(std::move(queuedMsgs)));

    Core::Config::Configuration config;
    config.set("egress.mode", "reply_to_origin");
    config.set("egress.input_channel", "pipeline_out");

    Proxy::Handler::UdpEgressHandler egressHandler(backend);
    Core::Handler::HandlerRunner runner(egressHandler);
    REQUIRE(runner.configure(config));

    // Run Egress handler to process message
    runner.startAsync();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    runner.stop();
    runner.wait();

    // Verify the client socket received the payload directly from Egress
    char recvBuf[256] { };
    timeval tv { .tv_sec = 1, .tv_usec = 0 };
    ::setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ssize_t received = ::recv(clientFd, recvBuf, sizeof(recvBuf), 0);

    REQUIRE(received == static_cast<ssize_t>(expectedPayload.size()));
    CHECK(std::string_view(recvBuf, received) == expectedPayload);

    ::close(clientFd);
}

TEST_CASE("UdpEgressHandler restores IPv6 client metadata and transmits reply over UDPv6", "[handler][egress][ipv6]")
{
    int clientFd = ::socket(AF_INET6, SOCK_DGRAM, 0);
    if (clientFd < 0) {
        return; // Skip if IPv6 is disabled on the host kernel
    }

    sockaddr_in6 clientAddr { };
    clientAddr.sin6_family = AF_INET6;
    clientAddr.sin6_addr = in6addr_loopback;
    clientAddr.sin6_port = 0;
    if (::bind(clientFd, reinterpret_cast<sockaddr*>(&clientAddr), sizeof(clientAddr)) != 0) {
        ::close(clientFd);
        return;
    }

    socklen_t addrLen = sizeof(clientAddr);
    REQUIRE(::getsockname(clientFd, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen) == 0);

    Core::Net::UdpMetadata meta { };
    meta.addressFamily = 6;
    std::memcpy(meta.srcIpv6, &clientAddr.sin6_addr, sizeof(meta.srcIpv6));
    meta.srcPort = clientAddr.sin6_port;
    meta.dstPort = htons(9999);

    std::string expectedPayload = "<order_receipt ipv6=\"true\"/>";
    auto envelope = Backend::MessageView::pack(
        meta,
        Common::Memory::ByteView(expectedPayload))
                        .value();

    DummyTestBackend backend;
    std::vector<std::vector<std::byte>> queuedMsgs { envelope };
    backend.queueInputChannel(std::make_unique<MockTestInputChannel>(std::move(queuedMsgs)));

    Core::Config::Configuration config;
    config.set("egress.mode", "reply_to_origin");
    config.set("egress.input_channel", "pipeline_out");

    Proxy::Handler::UdpEgressHandler egressHandler(backend);
    Core::Handler::HandlerRunner runner(egressHandler);
    REQUIRE(runner.configure(config));

    runner.startAsync();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    runner.stop();
    runner.wait();

    char recvBuf[256] { };
    timeval tv { .tv_sec = 1, .tv_usec = 0 };
    ::setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ssize_t received = ::recv(clientFd, recvBuf, sizeof(recvBuf), 0);

    REQUIRE(received == static_cast<ssize_t>(expectedPayload.size()));
    CHECK(std::string_view(recvBuf, received) == expectedPayload);

    ::close(clientFd);
}

TEST_CASE("UdpEgressHandler vectorized batch forward transmits all payloads in forward mode", "[handler][egress][batch]")
{
    int targetFd = ::socket(AF_INET, SOCK_DGRAM, 0);
    REQUIRE(targetFd >= 0);

    sockaddr_in targetAddr { };
    targetAddr.sin_family = AF_INET;
    targetAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    targetAddr.sin_port = 0;
    REQUIRE(::bind(targetFd, reinterpret_cast<sockaddr*>(&targetAddr), sizeof(targetAddr)) == 0);

    socklen_t addrLen = sizeof(targetAddr);
    REQUIRE(::getsockname(targetFd, reinterpret_cast<sockaddr*>(&targetAddr), &addrLen) == 0);
    std::uint16_t targetPort = ntohs(targetAddr.sin_port);

    const std::string payload1 = "BatchPacket1";
    const std::string payload2 = "BatchPacket2";
    auto env1 = Backend::MessageView::packRaw(Common::Memory::ByteView(payload1)).value();
    auto env2 = Backend::MessageView::packRaw(Common::Memory::ByteView(payload2)).value();

    DummyTestBackend backend;
    std::vector<std::vector<std::byte>> queuedMsgs { env1, env2 };
    backend.queueInputChannel(std::make_unique<MockTestInputChannel>(std::move(queuedMsgs)));

    Core::Config::Configuration config;
    config.set("egress.mode", "forward");
    config.set("egress.dest_host", "127.0.0.1");
    config.set("egress.dest_port", std::to_string(targetPort));
    config.set("egress.input_channel", "pipeline_out");

    Proxy::Handler::UdpEgressHandler egressHandler(backend);
    Core::Handler::HandlerRunner runner(egressHandler);
    REQUIRE(runner.configure(config));

    auto v1 = Backend::MessageView::from(Common::Memory::ByteView(env1));
    auto v2 = Backend::MessageView::from(Common::Memory::ByteView(env2));
    REQUIRE(v1.has_value());
    REQUIRE(v2.has_value());

    std::array<Backend::MessageView, 2> batch { *v1, *v2 };
    std::array<Backend::MessageResult, 2> results { };
    egressHandler.onBatchReceived("pipeline_out", batch, results);

    timeval tv { .tv_sec = 1, .tv_usec = 0 };
    ::setsockopt(targetFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char recvBuf1[256] { };
    ssize_t r1 = ::recv(targetFd, recvBuf1, sizeof(recvBuf1), 0);
    REQUIRE(r1 == static_cast<ssize_t>(payload1.size()));
    CHECK(std::string_view(recvBuf1, r1) == payload1);

    char recvBuf2[256] { };
    ssize_t r2 = ::recv(targetFd, recvBuf2, sizeof(recvBuf2), 0);
    REQUIRE(r2 == static_cast<ssize_t>(payload2.size()));
    CHECK(std::string_view(recvBuf2, r2) == payload2);

    ::close(targetFd);
}
