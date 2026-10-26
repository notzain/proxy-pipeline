#include "UdpEgressHandler.hpp"
#include "UdpIngressHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/IInputChannel.hpp"
#include "backend/IOutputChannel.hpp"
#include "core/config/Configuration.hpp"
#include "core/handler/HandlerRunner.hpp"
#include "core/message/MessageEnvelope.hpp"
#include "core/message/MessageView.hpp"
#include "core/net/UdpInputChannel.hpp"
#include "core/net/UdpOutputChannel.hpp"
#include <arpa/inet.h>
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
#include <vector>

namespace {

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
    bool send(std::span<const std::byte> message) override
    {
        std::lock_guard lock(m_mutex);
        std::string str(reinterpret_cast<const char*>(message.data()), message.size());
        m_sentMessages.push_back(std::move(str));
        return true;
    }

    std::size_t sendBatch(std::span<const std::span<const std::byte>> messages) override
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

    std::size_t receiveInto(std::span<std::byte> destination, Timeout) override
    {
        if (m_index < m_messages.size()) {
            const auto& msg = m_messages[m_index++];
            std::size_t count = std::min(destination.size(), msg.size());
            std::memcpy(destination.data(), msg.data(), count);
            return count;
        }
        return 0;
    }

    std::size_t receiveBatch(
        std::span<const std::span<std::byte>> destinations,
        std::span<std::size_t> outSizes,
        Timeout timeout) override
    {
        std::size_t count = 0;
        while (count < destinations.size()) {
            std::size_t bytes = receiveInto(destinations[count], timeout);
            if (bytes > 0) {
                outSizes[count++] = bytes;
            } else {
                break;
            }
        }
        return count;
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

TEST_CASE("Core::Message::MessageEnvelope and MessageView serialize and parse", "[message][envelope]")
{
    std::string testPayload = "<order><item>Widget</item></order>";
    std::span<const std::byte> payloadSpan(
        reinterpret_cast<const std::byte*>(testPayload.data()),
        testPayload.size());

    SECTION("Pack raw envelope")
    {
        auto env = Core::Message::MessageEnvelope::packRaw(payloadSpan);
        auto view = Core::Message::MessageView::from(env);

        REQUIRE(view.has_value());
        CHECK(view->protocol() == Core::Message::ProtocolId::Raw);
        CHECK(view->metadata().empty());
        CHECK(view->payload().size() == testPayload.size());

        std::string_view parsedPayload(reinterpret_cast<const char*>(view->payload().data()), view->payload().size());
        CHECK(parsedPayload == testPayload);
    }

    SECTION("Pack UDP envelope with metadata")
    {
        Core::Message::UdpMetadata meta { };
        meta.srcIp = 0x0100007F; // 127.0.0.1
        meta.srcPort = htons(54321);
        meta.dstIp = 0x0100007F;
        meta.dstPort = htons(9123);

        auto env = Core::Message::MessageEnvelope::packUdp(meta, payloadSpan);
        auto view = Core::Message::MessageView::from(env);

        REQUIRE(view.has_value());
        CHECK(view->protocol() == Core::Message::ProtocolId::Udp);

        const auto* parsedMeta = view->getMetadata<Core::Message::UdpMetadata>();
        REQUIRE(parsedMeta != nullptr);
        CHECK(parsedMeta->srcPort == htons(54321));
        CHECK(parsedMeta->dstPort == htons(9123));

        std::string_view parsedPayload(reinterpret_cast<const char*>(view->payload().data()), view->payload().size());
        CHECK(parsedPayload == testPayload);
    }

    SECTION("Mutate payload using withPayload")
    {
        Core::Message::UdpMetadata meta { };
        meta.srcPort = htons(1122);
        auto env = Core::Message::MessageEnvelope::packUdp(meta, payloadSpan);
        auto view = Core::Message::MessageView::from(env);
        REQUIRE(view.has_value());

        std::string updatedPayload = "<order><item>SuperWidget</item><qty>10</qty></order>";
        std::span<const std::byte> updatedSpan(
            reinterpret_cast<const std::byte*>(updatedPayload.data()),
            updatedPayload.size());

        auto mutatedEnv = Core::Message::MessageEnvelope::withPayload(*view, updatedSpan);
        auto mutatedView = Core::Message::MessageView::from(mutatedEnv);

        REQUIRE(mutatedView.has_value());
        CHECK(mutatedView->protocol() == Core::Message::ProtocolId::Udp);
        CHECK(mutatedView->getMetadata<Core::Message::UdpMetadata>()->srcPort == htons(1122));

        std::string_view parsedMutated(reinterpret_cast<const char*>(mutatedView->payload().data()), mutatedView->payload().size());
        CHECK(parsedMutated == updatedPayload);
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
        std::size_t received = channel.receiveInto(buffer, std::chrono::milliseconds(500));

        REQUIRE(received > expectedPayload.size());
        auto view = Core::Message::MessageView::from(std::span<const std::byte>(buffer.data(), received));
        REQUIRE(view.has_value());
        CHECK(view->protocol() == Core::Message::ProtocolId::Udp);

        const auto* meta = view->getMetadata<Core::Message::UdpMetadata>();
        REQUIRE(meta != nullptr);
        CHECK(ntohs(meta->dstPort) == testPort);

        std::string_view receivedView(reinterpret_cast<const char*>(view->payload().data()), view->payload().size());
        CHECK(receivedView == expectedPayload);
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

        auto view = Core::Message::MessageView::from(
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(sentMessages.front().data()),
                sentMessages.front().size()));

        REQUIRE(view.has_value());
        CHECK(view->protocol() == Core::Message::ProtocolId::Udp);
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
    Core::Message::UdpMetadata meta { };
    meta.srcIp = htonl(INADDR_LOOPBACK);
    meta.srcPort = clientAddr.sin_port;
    meta.dstPort = htons(9999);

    std::string expectedPayload = "<order_receipt status=\"confirmed\"/>";
    auto envelope = Core::Message::MessageEnvelope::packUdp(
        meta,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(expectedPayload.data()), expectedPayload.size()));

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

    Core::Message::UdpMetadata meta { };
    meta.addressFamily = 6;
    std::memcpy(meta.srcIpv6, &clientAddr.sin6_addr, sizeof(meta.srcIpv6));
    meta.srcPort = clientAddr.sin6_port;
    meta.dstPort = htons(9999);

    std::string expectedPayload = "<order_receipt ipv6=\"true\"/>";
    auto envelope = Core::Message::MessageEnvelope::packUdp(
        meta,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(expectedPayload.data()), expectedPayload.size()));

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
