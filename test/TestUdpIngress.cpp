#include "UdpIngressHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/IOutputChannel.hpp"
#include "core/config/Configuration.hpp"
#include "core/net/UdpInputChannel.hpp"
#include <arpa/inet.h>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <memory>
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
        std::string str(reinterpret_cast<const char*>(message.data()), message.size());
        m_sentMessages.push_back(std::move(str));
        return true;
    }

    std::size_t sendBatch(std::span<const std::span<const std::byte>> messages) override
    {
        std::size_t count = 0;
        for (const auto& span : messages) {
            if (send(span)) {
                ++count;
            }
        }
        return count;
    }

    const std::vector<std::string>& getSentMessages() const noexcept
    {
        return m_sentMessages;
    }

    void clear()
    {
        m_sentMessages.clear();
    }

private:
    std::vector<std::string> m_sentMessages;
};

class DummyTestBackend : public ::Backend::IBackend {
public:
    void configure(const ::Backend::IConfig&) override { }

    std::unique_ptr<::Backend::IInputChannel> createInputChannel(std::string_view, const ::Backend::IConfig&) override
    {
        return nullptr;
    }

    std::unique_ptr<::Backend::IOutputChannel> createOutputChannel(std::string_view, const ::Backend::IConfig&) override
    {
        auto channel = std::make_unique<TestOutputChannel>();
        m_lastCreatedOutput = channel.get();
        return channel;
    }

    TestOutputChannel* m_lastCreatedOutput { nullptr };
};

} // namespace

TEST_CASE("Core::Net::UdpInputChannel receives external UDP datagrams", "[net][udp]")
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

        std::array<std::byte, 1024> buffer;
        std::size_t received = channel.receiveInto(buffer, std::chrono::milliseconds(500));

        REQUIRE(received == expectedPayload.size());
        std::string_view receivedView(reinterpret_cast<const char*>(buffer.data()), received);
        CHECK(receivedView == expectedPayload);
    }

    SECTION("Receive returns optional buffer")
    {
        std::string expectedPayload = "PING_UDP_TEST";
        sendTestUdpDatagram("127.0.0.1", testPort, expectedPayload);

        auto optBuffer = channel.receive(std::chrono::milliseconds(500));
        REQUIRE(optBuffer.has_value());
        REQUIRE(optBuffer->size() == expectedPayload.size());

        std::string_view receivedView(reinterpret_cast<const char*>(optBuffer->data()), optBuffer->size());
        CHECK(receivedView == expectedPayload);
    }

    SECTION("Timeout on no datagram")
    {
        std::array<std::byte, 64> buffer;
        std::size_t received = channel.receiveInto(buffer, std::chrono::milliseconds(50));
        CHECK(received == 0);
    }
}

TEST_CASE("UdpIngressHandler ingests UDP stream and forwards pure payload to pipeline backend", "[handler][ingress]")
{
    constexpr std::uint16_t testPort = 19877;

    Core::Config::Configuration config;
    config.set("ingress.host", "127.0.0.1");
    config.set("ingress.port", std::to_string(testPort));
    config.set("ingress.output_channel", "pipeline_ingress_queue");

    DummyTestBackend backend;
    Proxy::Handler::UdpIngressHandler handler;

    REQUIRE(handler.configure(config, backend));
    REQUIRE(backend.m_lastCreatedOutput != nullptr);

    SECTION("Single datagram received and forwarded")
    {
        std::string testOrder = "<order><customer>Alice</customer><item>Widget</item><quantity>42</quantity></order>";
        sendTestUdpDatagram("127.0.0.1", testPort, testOrder);

        handler.startAsync();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        handler.stop();
        handler.wait();

        const auto& sentMessages = backend.m_lastCreatedOutput->getSentMessages();
        REQUIRE(sentMessages.size() >= 1);
        CHECK(sentMessages.front() == testOrder);
    }

    SECTION("Burst of datagrams drained as a multi-message batch")
    {
        for (int i = 0; i < 8; ++i) {
            sendTestUdpDatagram("127.0.0.1", testPort, "BatchOrder-" + std::to_string(i));
        }

        handler.startAsync();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        handler.stop();
        handler.wait();

        const auto& sentMessages = backend.m_lastCreatedOutput->getSentMessages();
        REQUIRE(sentMessages.size() == 8);
    }
}
