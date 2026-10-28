#include "RouteHandler.hpp"
#include "backend/IBackend.hpp"
#include "backend/IInputChannel.hpp"
#include "backend/IOutputChannel.hpp"
#include "common/memory/ByteView.hpp"
#include "core/config/Configuration.hpp"
#include "core/message/MessageEnvelope.hpp"
#include "core/message/ProtocolId.hpp"
#include "core/message/UdpMetadata.hpp"
#include "core/simulation/SimulationBackend.hpp"
#include <arpa/inet.h>
#include <catch2/catch_test_macros.hpp>
#include <mutex>
#include <string>
#include <vector>

namespace {

class MockChannel : public ::Backend::IOutputChannel {
public:
    explicit MockChannel(std::string name)
        : ::Backend::IOutputChannel(std::move(name))
    {
    }

    bool send(Common::Compat::span<const std::byte> message) override
    {
        std::lock_guard lock(m_mutex);
        auto optView = Core::Message::MessageView::from(message);
        std::string payloadStr;
        if (optView) {
            payloadStr = std::string(reinterpret_cast<const char*>(optView->payload().data()), optView->payload().size());
        } else {
            payloadStr = std::string(reinterpret_cast<const char*>(message.data()), message.size());
        }
        sentPayloads.push_back(payloadStr);
        return true;
    }

    std::size_t sendBatch(Common::Compat::span<const Common::Compat::span<const std::byte>> messages) override
    {
        for (const auto& msg : messages) {
            send(msg);
        }
        return messages.size();
    }

    std::mutex m_mutex;
    std::vector<std::string> sentPayloads;
};

} // namespace

TEST_CASE("RouteHandler content-based routing rules", "[handler][router]")
{
    Core::Simulation::SimulationBackend backend;
    Proxy::Handler::RouteHandler router(backend);

    auto vipChan = std::make_unique<MockChannel>("vip_out");
    auto stdChan = std::make_unique<MockChannel>("std_out");
    auto defChan = std::make_unique<MockChannel>("def_out");

    auto* vipPtr = vipChan.get();
    auto* stdPtr = stdChan.get();
    auto* defPtr = defChan.get();

    router.registerOutputChannel(std::move(vipChan));
    router.registerOutputChannel(std::move(stdChan));
    router.setDefaultChannel(router.registerOutputChannel(std::move(defChan)));

    // Rule 1: contains "VIP" -> vip_out
    Proxy::Handler::RouteRule ruleVip(Proxy::Handler::MatchCriteria::PayloadContains { "VIP" }, "vip_out");
    ruleVip.bindTargetChannel(vipPtr);
    router.addRule(std::move(ruleVip));

    // Rule 2: prefix "STD:" -> std_out
    Proxy::Handler::RouteRule ruleStd(Proxy::Handler::MatchCriteria::PayloadPrefix { "STD:" }, "std_out");
    ruleStd.bindTargetChannel(stdPtr);
    router.addRule(std::move(ruleStd));

    SECTION("Single message routing")
    {
        std::string msg1 = "Hello VIP Customer";
        auto env1 = Core::Message::MessageEnvelope::packRaw(Common::Memory::ByteView(msg1));
        auto view1 = Core::Message::MessageView::from(env1);
        REQUIRE(view1.has_value());

        std::string msg2 = "STD: Regular Customer";
        auto env2 = Core::Message::MessageEnvelope::packRaw(Common::Memory::ByteView(msg2));
        auto view2 = Core::Message::MessageView::from(env2);
        REQUIRE(view2.has_value());

        std::string msg3 = "Other message";
        auto env3 = Core::Message::MessageEnvelope::packRaw(Common::Memory::ByteView(msg3));
        auto view3 = Core::Message::MessageView::from(env3);
        REQUIRE(view3.has_value());

        router.onMessageReceived("in", *view1);
        router.onMessageReceived("in", *view2);
        router.onMessageReceived("in", *view3);

        REQUIRE(vipPtr->sentPayloads.size() == 1);
        CHECK(vipPtr->sentPayloads[0] == "Hello VIP Customer");

        REQUIRE(stdPtr->sentPayloads.size() == 1);
        CHECK(stdPtr->sentPayloads[0] == "STD: Regular Customer");

        REQUIRE(defPtr->sentPayloads.size() == 1);
        CHECK(defPtr->sentPayloads[0] == "Other message");
    }

    SECTION("Vectorized batch routing")
    {
        std::string msgVip1 = "VIP 1";
        std::string msgVip2 = "VIP 2";
        std::string msgStd = "STD: Batch";
        std::string msgDef = "Default Fallback";

        auto env1 = Core::Message::MessageEnvelope::packRaw(Common::Memory::ByteView(msgVip1));
        auto env2 = Core::Message::MessageEnvelope::packRaw(Common::Memory::ByteView(msgStd));
        auto env3 = Core::Message::MessageEnvelope::packRaw(Common::Memory::ByteView(msgVip2));
        auto env4 = Core::Message::MessageEnvelope::packRaw(Common::Memory::ByteView(msgDef));

        std::vector<Core::Message::MessageView> batch = {
            *Core::Message::MessageView::from(env1),
            *Core::Message::MessageView::from(env2),
            *Core::Message::MessageView::from(env3),
            *Core::Message::MessageView::from(env4)
        };

        std::array<Core::Message::MessageResult, 4> results;
        router.onBatchReceived("in", batch, results);
        CHECK(std::get<Core::Message::Forwarded>(results[0]).channel == "vip_out");
        CHECK(std::get<Core::Message::Forwarded>(results[1]).channel == "std_out");
        CHECK(std::get<Core::Message::Forwarded>(results[2]).channel == "vip_out");
        CHECK(std::get<Core::Message::Forwarded>(results[3]).channel == "def_out");

        REQUIRE(vipPtr->sentPayloads.size() == 2);
        CHECK(vipPtr->sentPayloads[0] == "VIP 1");
        CHECK(vipPtr->sentPayloads[1] == "VIP 2");

        REQUIRE(stdPtr->sentPayloads.size() == 1);
        CHECK(stdPtr->sentPayloads[0] == "STD: Batch");

        REQUIRE(defPtr->sentPayloads.size() == 1);
        CHECK(defPtr->sentPayloads[0] == "Default Fallback");
    }

    SECTION("Client port metadata routing")
    {
        Proxy::Handler::RouteHandler portRouter(backend);
        auto portChan = std::make_unique<MockChannel>("port_out");
        auto* portPtr = portChan.get();
        portRouter.registerOutputChannel(std::move(portChan));

        Proxy::Handler::RouteRule portRule(Proxy::Handler::MatchCriteria::ClientPort { 9050 }, "port_out");
        portRule.bindTargetChannel(portPtr);
        portRouter.addRule(std::move(portRule));

        Core::Message::UdpMetadata meta;
        meta.srcPort = htons(9050);
        meta.dstPort = htons(8080);
        meta.srcIp = inet_addr("127.0.0.1");

        std::string payload = "Datagram from 9050";
        auto env = Core::Message::MessageEnvelope::packUdp(
            meta,
            Common::Memory::ByteView(payload));
        auto view = Core::Message::MessageView::from(env);
        REQUIRE(view.has_value());

        portRouter.onMessageReceived("udp_in", *view);
        REQUIRE(portPtr->sentPayloads.size() == 1);
        CHECK(portPtr->sentPayloads[0] == payload);
    }
}
