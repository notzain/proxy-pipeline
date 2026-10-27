#include "core/config/Configuration.hpp"
#include "core/handler/HandlerRunner.hpp"
#include "core/handler/IHandler.hpp"
#include "core/message/MessageEnvelope.hpp"
#include "core/message/MessageView.hpp"
#include "core/simulation/MockInputChannel.hpp"
#include "core/simulation/MockOutputChannel.hpp"
#include "core/simulation/PcapFormat.hpp"
#include "core/simulation/PcapReader.hpp"
#include "core/simulation/PcapWriter.hpp"
#include "core/simulation/SimulationBackend.hpp"
#include "core/simulation/SimulationClock.hpp"
#include <arpa/inet.h>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

TEST_CASE("Core::Simulation::SimulationClock virtual stepping and realtime", "[simulation]")
{
    Core::Simulation::SimulationClock clock(Core::Simulation::SimulationClock::Mode::Virtual, 1'000'000ULL);
    REQUIRE(clock.nowNs() == 1'000'000ULL);

    // Step clock deterministically
    clock.advance(std::chrono::microseconds(50));
    REQUIRE(clock.nowNs() == 1'050'000ULL);

    clock.setTimeNs(5'000'000ULL);
    REQUIRE(clock.nowNs() == 5'000'000ULL);

    clock.reset(100'000ULL);
    REQUIRE(clock.nowNs() == 100'000ULL);
}

TEST_CASE("Core::Simulation::PcapWriter and PcapReader end-to-end round trip", "[simulation][pcap]")
{
    const std::string testPcapFile = "/tmp/test_proxy_roundtrip.pcap";
    if (std::filesystem::exists(testPcapFile)) {
        std::filesystem::remove(testPcapFile);
    }

    // 1. Write UDP packets into PCAP file
    {
        Core::Simulation::PcapWriter writer;
        REQUIRE(writer.open(testPcapFile, true)); // Nanosecond precision
        REQUIRE(writer.isOpen());

        std::uint32_t clientIp = inet_addr("192.168.1.100");
        std::uint32_t serverIp = inet_addr("192.168.1.1");
        std::uint16_t clientPort = htons(55555);
        std::uint16_t serverPort = htons(9000);

        std::string payload1 = "<order><id>1</id><sym>AAPL</sym></order>";
        std::span<const std::byte> span1(reinterpret_cast<const std::byte*>(payload1.data()), payload1.size());
        REQUIRE(writer.writeUdpPacket(clientIp, serverIp, clientPort, serverPort, span1, 1'700'000'000'100'000ULL));

        std::string payload2 = "<order><id>2</id><sym>MSFT</sym></order>";
        std::span<const std::byte> span2(reinterpret_cast<const std::byte*>(payload2.data()), payload2.size());
        REQUIRE(writer.writeUdpPacket(clientIp, serverIp, clientPort, serverPort, span2, 1'700'000'000'250'000ULL));

        writer.close();
        REQUIRE(!writer.isOpen());
    }

    // 2. Read back packets using PcapReader
    {
        Core::Simulation::PcapReader reader;
        REQUIRE(reader.open(testPcapFile));
        REQUIRE(reader.isOpen());
        REQUIRE(reader.isNanosecondPrecision());

        auto pkt1 = reader.readNextUdpPacket();
        REQUIRE(pkt1.has_value());
        REQUIRE(pkt1->timestampNs == 1'700'000'000'100'000ULL);
        REQUIRE(pkt1->srcPort == htons(55555));
        REQUIRE(pkt1->dstPort == htons(9000));
        std::string readPayload1(reinterpret_cast<const char*>(pkt1->payload.data()), pkt1->payload.size());
        REQUIRE(readPayload1 == "<order><id>1</id><sym>AAPL</sym></order>");

        auto pkt2 = reader.readNextUdpPacket();
        REQUIRE(pkt2.has_value());
        REQUIRE(pkt2->timestampNs == 1'700'000'000'250'000ULL);
        std::string readPayload2(reinterpret_cast<const char*>(pkt2->payload.data()), pkt2->payload.size());
        REQUIRE(readPayload2 == "<order><id>2</id><sym>MSFT</sym></order>");

        // EOF check
        auto pkt3 = reader.readNextUdpPacket();
        REQUIRE(!pkt3.has_value());

        // Test rewind
        REQUIRE(reader.rewind());
        auto rewoundPkt = reader.readNextUdpPacket();
        REQUIRE(rewoundPkt.has_value());
        REQUIRE(rewoundPkt->timestampNs == 1'700'000'000'100'000ULL);

        reader.close();
    }

    std::filesystem::remove(testPcapFile);
}

TEST_CASE("Core::Simulation::MockInputChannel and MockOutputChannel pipeline simulation", "[simulation]")
{
    Core::Simulation::SimulationClock clock(Core::Simulation::SimulationClock::Mode::Virtual, 1'000'000ULL);

    Core::Simulation::MockInputChannel inChannel(&clock);
    Core::Simulation::MockOutputChannel outChannel(&clock);

    // Push two messages: one immediate, one scheduled 50us into virtual future
    inChannel.pushMessage("Message 1", 1'000'000ULL);
    inChannel.pushMessage("Message 2", 1'050'000ULL);

    REQUIRE(inChannel.pendingCount() == 2);

    // At time 1'000'000ULL, only Message 1 is ready
    auto msg1 = inChannel.receive(std::chrono::milliseconds(0));
    REQUIRE(msg1.hasValue());
    std::string text1(reinterpret_cast<const char*>(msg1.value().data()), msg1.value().size());
    REQUIRE(text1 == "Message 1");

    // Message 2 should not be returned yet because clock has not advanced
    auto msg2Pending = inChannel.receive(std::chrono::milliseconds(0));
    REQUIRE_FALSE(msg2Pending.hasValue());
    CHECK(Backend::isReceiveTimeout(msg2Pending.error()));

    // Advance clock by 50 microseconds
    clock.advance(std::chrono::microseconds(50));
    REQUIRE(clock.nowNs() == 1'050'000ULL);

    // Now Message 2 is visible
    auto msg2 = inChannel.receive(std::chrono::milliseconds(0));
    REQUIRE(msg2.hasValue());
    std::string text2(reinterpret_cast<const char*>(msg2.value().data()), msg2.value().size());
    REQUIRE(text2 == "Message 2");

    // Test MockOutputChannel recording
    std::string reply = "Ack Message 2";
    std::span<const std::byte> replySpan(reinterpret_cast<const std::byte*>(reply.data()), reply.size());
    REQUIRE(outChannel.send(replySpan));

    REQUIRE(outChannel.messageCount() == 1);
    REQUIRE(outChannel.containsPayload("Ack Message 2"));
    auto records = outChannel.getRecordedMessages();
    REQUIRE(records.size() == 1);
    REQUIRE(records[0].recordedTimeNs == 1'050'000ULL);
}

TEST_CASE("Core::Simulation::SimulationBackend integration with IHandler", "[simulation]")
{
    Core::Simulation::SimulationClock clock(Core::Simulation::SimulationClock::Mode::Virtual, 100'000ULL);
    Core::Simulation::SimulationBackend simBackend(&clock);

    Core::Config::Configuration config;

    class SimEchoHandler : public Core::Handler::IHandler {
    public:
        explicit SimEchoHandler(::Backend::IBackend& backend)
            : Core::Handler::IHandler(backend)
        {
        }

        Core::Memory::ObserverPtr<::Backend::IOutputChannel> out;

        bool onConfigure(const Core::Config::Configuration& cfg) override
        {
            registerInputChannel(backend().createInputChannel("ingress", cfg));
            out = registerOutputChannel(backend().createOutputChannel("egress", cfg));
            return true;
        }

        Core::Message::MessageResult onMessageReceived(std::string_view, const Core::Message::MessageView& msg) override
        {
            if (out) {
                out->send(msg.payload());
                return Core::Message::Forwarded { .channel = "egress" };
            }
            return Core::Message::Consumed { };
        }
    };

    SimEchoHandler handler(simBackend);
    Core::Handler::HandlerRunner runner(handler);
    runner.setExecutionModel(Core::Handler::SequentialModel { });
    REQUIRE(runner.configure(config));

    auto inCh = simBackend.getInputChannel("ingress");
    auto outCh = simBackend.getOutputChannel("egress");
    REQUIRE(inCh != nullptr);
    REQUIRE(outCh != nullptr);

    // Push envelope into ingress
    std::string payload = "Deterministic Sim Payload";
    std::span<const std::byte> pSpan(reinterpret_cast<const std::byte*>(payload.data()), payload.size());
    auto env = Core::Message::MessageEnvelope::packRaw(pSpan, 100'000ULL);
    inCh->pushMessage(env, 100'000ULL);

    // Run handler asynchronously to process message
    runner.startAsync();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    runner.stop();
    runner.wait();

    // Verify message forwarded to mock egress with exact virtual timing
    REQUIRE(outCh->messageCount() == 1);
    REQUIRE(outCh->containsPayload("Deterministic Sim Payload"));
    auto records = outCh->getRecordedMessages();
    REQUIRE(records[0].recordedTimeNs == 100'000ULL);

    // Verify MessageResult was reported to SimulationBackend
    auto results = simBackend.getRecordedResults();
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].inputChannel == "ingress");
    REQUIRE(results[0].resultType == "Forwarded");
    REQUIRE(results[0].outputChannel == "egress");
    REQUIRE(results[0].recordedTimeNs == 100'000ULL);
}

TEST_CASE("IHandler reports mixed per-message batch results to the backend", "[simulation][batch]")
{
    Core::Simulation::SimulationBackend backend;

    class BatchResultHandler : public Core::Handler::IHandler {
    public:
        using Core::Handler::IHandler::IHandler;

        void onBatchReceived(
            std::string_view,
            std::span<const Core::Message::MessageView> messages,
            std::span<Core::Message::MessageResult> outResults) override
        {
            if (outResults.size() >= 2) {
                outResults[0] = Core::Message::Forwarded { .channel = "accepted" };
                outResults[1] = Core::Message::Dropped {
                    .reason = "invalid test message",
                    .errorCode = 400
                };
            }
        }
    };

    BatchResultHandler handler(backend);
    std::string firstPayload = "valid";
    std::string secondPayload = "invalid";
    auto firstEnvelope = Core::Message::MessageEnvelope::packRaw(
        std::as_bytes(std::span(firstPayload.data(), firstPayload.size())));
    auto secondEnvelope = Core::Message::MessageEnvelope::packRaw(
        std::as_bytes(std::span(secondPayload.data(), secondPayload.size())));
    std::array<Core::Message::MessageView, 2> messages {
        *Core::Message::MessageView::from(firstEnvelope),
        *Core::Message::MessageView::from(secondEnvelope)
    };

    handler.processBatch("input", messages);

    const auto results = backend.getRecordedResults();
    REQUIRE(results.size() == messages.size());
    CHECK(results[0].inputChannel == "input");
    CHECK(results[0].resultType == "Forwarded");
    CHECK(results[0].outputChannel == "accepted");
    CHECK(results[1].inputChannel == "input");
    CHECK(results[1].resultType == "Dropped");
    CHECK(results[1].dropReason == "invalid test message");
    CHECK(results[1].errorCode == 400);
}
