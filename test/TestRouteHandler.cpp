#include "RouteHandler.hpp"
#include "RouteRuleBuilder.hpp"
#include "backend/IBackend.hpp"
#include "backend/channel/IInputChannel.hpp"
#include "backend/channel/IOutputChannel.hpp"
#include "backend/message/MessageView.hpp"
#include "backend/message/ProtocolId.hpp"
#include "common/memory/ByteView.hpp"
#include "core/config/Configuration.hpp"
#include "core/net/UdpMetadata.hpp"
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

    using ::Backend::IOutputChannel::send;
    using ::Backend::IOutputChannel::sendBatch;

    bool send(Common::Compat::span<const std::byte> message) override
    {
        std::lock_guard lock(m_mutex);
        auto optView = Backend::MessageView::from(message);
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
        auto env1 = Backend::MessageView::packRaw(Common::Memory::ByteView(msg1)).value();
        auto view1 = Backend::MessageView::from(env1);
        REQUIRE(view1.has_value());

        std::string msg2 = "STD: Regular Customer";
        auto env2 = Backend::MessageView::packRaw(Common::Memory::ByteView(msg2)).value();
        auto view2 = Backend::MessageView::from(env2);
        REQUIRE(view2.has_value());

        std::string msg3 = "Other message";
        auto env3 = Backend::MessageView::packRaw(Common::Memory::ByteView(msg3)).value();
        auto view3 = Backend::MessageView::from(env3);
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

        auto env1 = Backend::MessageView::packRaw(Common::Memory::ByteView(msgVip1)).value();
        auto env2 = Backend::MessageView::packRaw(Common::Memory::ByteView(msgStd)).value();
        auto env3 = Backend::MessageView::packRaw(Common::Memory::ByteView(msgVip2)).value();
        auto env4 = Backend::MessageView::packRaw(Common::Memory::ByteView(msgDef)).value();

        std::vector<Backend::MessageView> batch = {
            *Backend::MessageView::from(env1),
            *Backend::MessageView::from(env2),
            *Backend::MessageView::from(env3),
            *Backend::MessageView::from(env4)
        };

        std::array<Backend::MessageResult, 4> results;
        router.onBatchReceived("in", batch, results);
        CHECK(std::get<Backend::Forwarded>(results[0]).channel == "vip_out");
        CHECK(std::get<Backend::Forwarded>(results[1]).channel == "std_out");
        CHECK(std::get<Backend::Forwarded>(results[2]).channel == "vip_out");
        CHECK(std::get<Backend::Forwarded>(results[3]).channel == "def_out");

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

        Core::Net::UdpMetadata meta;
        meta.srcPort = htons(9050);
        meta.dstPort = htons(8080);
        meta.srcIp = inet_addr("127.0.0.1");

        std::string payload = "Datagram from 9050";
        auto env = Backend::MessageView::pack(
            meta,
            Common::Memory::ByteView(payload))
                       .value();
        auto view = Backend::MessageView::from(env);
        REQUIRE(view.has_value());

        portRouter.onMessageReceived("udp_in", *view);
        REQUIRE(portPtr->sentPayloads.size() == 1);
        CHECK(portPtr->sentPayloads[0] == payload);
    }
}

TEST_CASE("RouteHandler composable rules, fluent builder and rejection reasons", "[handler][router][composable]")
{
    Core::Simulation::SimulationBackend backend;
    Proxy::Handler::RouteHandler router(backend);

    auto validChan = std::make_unique<MockChannel>("valid_out");
    auto* validPtr = validChan.get();
    router.registerOutputChannel(std::move(validChan));

    // Rule 1: Text.contains('\n').times(3) -> Reject("Too many newlines", 422)
    auto rejectNewlines = Proxy::Handler::RouteRule::when(
        Proxy::Handler::Text::contains("\n").times(3))
                              .reject("Too many newlines", 422);
    router.addRule(std::move(rejectNewlines));

    // Rule 2: (Text.prefix("HDR:") AND Port.equals(5000)) -> ForwardTo("valid_out")
    auto validAdmin = Proxy::Handler::RouteRule::when(Proxy::Handler::Text::prefix("HDR:"))
                          .and_(Proxy::Handler::Port::equals(5000))
                          .forwardTo("valid_out");
    validAdmin.bindTargetChannel(validPtr);
    router.addRule(std::move(validAdmin));

    // Rule 3: Text.contains("SECRET") -> Reject("Access denied", 403)
    auto rejectSecret = Proxy::Handler::RouteRule::when(Proxy::Handler::Text::contains("SECRET"))
                            .reject("Access denied", 403);
    router.addRule(std::move(rejectSecret));

    SECTION("Rejection triggers Dropped with explicit reason and error code")
    {
        std::string multiLine = "Line1\nLine2\nLine3\nLine4";
        auto env1 = Backend::MessageView::packRaw(Common::Memory::ByteView(multiLine)).value();
        auto view1 = Backend::MessageView::from(env1);
        REQUIRE(view1.has_value());

        auto res1 = router.onMessageReceived("in", *view1);
        REQUIRE(res1.is<Backend::Dropped>());
        auto dropped = res1.get<Backend::Dropped>();
        REQUIRE(dropped.has_value());
        CHECK(dropped->get().reason == "Too many newlines");
        CHECK(dropped->get().errorCode == 422);
        CHECK(validPtr->sentPayloads.empty());
    }

    SECTION("Composite AND condition matching forwards correctly")
    {
        Core::Net::UdpMetadata meta;
        meta.srcPort = htons(5000);
        meta.dstPort = htons(8080);
        meta.srcIp = inet_addr("127.0.0.1");

        std::string payload = "HDR:Authorized packet";
        auto env = Backend::MessageView::pack(meta, Common::Memory::ByteView(payload)).value();
        auto view = Backend::MessageView::from(env);
        REQUIRE(view.has_value());

        auto res = router.onMessageReceived("in", *view);
        REQUIRE(res.is<Backend::Forwarded>());
        auto fwd = res.get<Backend::Forwarded>();
        REQUIRE(fwd.has_value());
        CHECK(fwd->get().channel == "valid_out");
        REQUIRE(validPtr->sentPayloads.size() == 1);
        CHECK(validPtr->sentPayloads[0] == payload);
    }

    SECTION("Composite AND failure falls through to subsequent rules")
    {
        // Port is 6000 instead of 5000, but payload has SECRET -> should hit Rule 3 reject
        Core::Net::UdpMetadata meta;
        meta.srcPort = htons(6000);
        meta.dstPort = htons(8080);
        meta.srcIp = inet_addr("127.0.0.1");

        std::string payload = "HDR:SECRET_DATA";
        auto env = Backend::MessageView::pack(meta, Common::Memory::ByteView(payload)).value();
        auto view = Backend::MessageView::from(env);
        REQUIRE(view.has_value());

        auto res = router.onMessageReceived("in", *view);
        REQUIRE(res.is<Backend::Dropped>());
        auto dropped = res.get<Backend::Dropped>();
        REQUIRE(dropped.has_value());
        CHECK(dropped->get().reason == "Access denied");
        CHECK(dropped->get().errorCode == 403);
    }

    SECTION("Vectorized batch correctly distinguishes Forwarded and Dropped")
    {
        Core::Net::UdpMetadata meta;
        meta.srcPort = htons(5000);
        meta.dstPort = htons(8080);
        meta.srcIp = inet_addr("127.0.0.1");

        std::string goodMsg = "HDR:Valid";
        std::string badMsg1 = "a\nb\nc\nd";
        std::string badMsg2 = "Contains SECRET token";

        auto env1 = Backend::MessageView::pack(meta, Common::Memory::ByteView(goodMsg)).value();
        auto env2 = Backend::MessageView::packRaw(Common::Memory::ByteView(badMsg1)).value();
        auto env3 = Backend::MessageView::packRaw(Common::Memory::ByteView(badMsg2)).value();

        std::vector<Backend::MessageView> batch = {
            *Backend::MessageView::from(env1),
            *Backend::MessageView::from(env2),
            *Backend::MessageView::from(env3)
        };

        std::array<Backend::MessageResult, 3> results;
        router.onBatchReceived("in", batch, results);

        REQUIRE(results[0].is<Backend::Forwarded>());
        CHECK(results[0].get<Backend::Forwarded>()->get().channel == "valid_out");

        REQUIRE(results[1].is<Backend::Dropped>());
        CHECK(results[1].get<Backend::Dropped>()->get().reason == "Too many newlines");

        REQUIRE(results[2].is<Backend::Dropped>());
        CHECK(results[2].get<Backend::Dropped>()->get().reason == "Access denied");

        REQUIRE(validPtr->sentPayloads.size() == 1);
        CHECK(validPtr->sentPayloads[0] == goodMsg);
    }
}

TEST_CASE("RouteHandler parses inline expr with AND, OR, NOT from config", "[handler][router][config][expr]")
{
    Core::Simulation::SimulationBackend backend;
    Proxy::Handler::RouteHandler router(backend);

    Core::Config::Configuration config;
    config.set("route.input_channel", "sim_in");
    config.set("route.rules", "rule_reject_complex,rule_admin");

    // Rule 1: expr = contains("\n", 3) or (prefix("BLOCK:") and not port == 9999) -> Reject
    config.set("route.rule.rule_reject_complex.expr", "contains(\"\\n\", 3) or (prefix(\"BLOCK:\") and not port == 9999)");
    config.set("route.rule.rule_reject_complex.action", "reject");
    config.set("route.rule.rule_reject_complex.reason", "Complex policy violation");
    config.set("route.rule.rule_reject_complex.error_code", "403");

    // Rule 2: expr = prefix("HDR:") and (port == 5000 or port == 8080) and not contains("BAD") -> Forward to sim_admin
    config.set("route.rule.rule_admin.expr", "prefix(\"HDR:\") and (port == 5000 or port == 8080) and not contains(\"BAD\")");
    config.set("route.rule.rule_admin.target", "sim_admin");
    config.set("route.rule.rule_admin.action", "forward");

    REQUIRE(router.onConfigure(config));
    REQUIRE(router.rules().size() == 2);

    SECTION("Expression OR matches first branch (newlines)")
    {
        std::string payload = "line1\nline2\nline3\nline4";
        auto env = Backend::MessageView::packRaw(Common::Memory::ByteView(payload)).value();
        auto view = Backend::MessageView::from(env);
        REQUIRE(view.has_value());

        auto res = router.onMessageReceived("sim_in", *view);
        REQUIRE(res.is<Backend::Dropped>());
        CHECK(res.get<Backend::Dropped>()->get().reason == "Complex policy violation");
        CHECK(res.get<Backend::Dropped>()->get().errorCode == 403);
    }

    SECTION("Expression OR matches second branch (BLOCK prefix and port != 9999)")
    {
        Core::Net::UdpMetadata meta;
        meta.srcPort = htons(1234);
        meta.dstPort = htons(80);
        meta.srcIp = inet_addr("127.0.0.1");

        std::string payload = "BLOCK: Suspicious activity";
        auto env = Backend::MessageView::pack(meta, Common::Memory::ByteView(payload)).value();
        auto view = Backend::MessageView::from(env);
        REQUIRE(view.has_value());

        auto res = router.onMessageReceived("sim_in", *view);
        REQUIRE(res.is<Backend::Dropped>());
        CHECK(res.get<Backend::Dropped>()->get().reason == "Complex policy violation");
    }

    SECTION("Whitelisted port 9999 bypasses the BLOCK reject rule")
    {
        Core::Net::UdpMetadata meta;
        meta.srcPort = htons(9999);
        meta.dstPort = htons(80);
        meta.srcIp = inet_addr("127.0.0.1");

        std::string payload = "BLOCK: Whitelisted maintenance bypass";
        auto env = Backend::MessageView::pack(meta, Common::Memory::ByteView(payload)).value();
        auto view = Backend::MessageView::from(env);
        REQUIRE(view.has_value());

        auto res = router.onMessageReceived("sim_in", *view);
        // Falls through and gets dropped with default 404 because no default channel was registered
        REQUIRE(res.is<Backend::Dropped>());
        CHECK(res.get<Backend::Dropped>()->get().errorCode == 404);
    }

    SECTION("Admin rule with nested AND, OR, NOT matches port 5000")
    {
        Core::Net::UdpMetadata meta;
        meta.srcPort = htons(5000);
        meta.dstPort = htons(80);
        meta.srcIp = inet_addr("127.0.0.1");

        std::string payload = "HDR:Good message";
        auto env = Backend::MessageView::pack(meta, Common::Memory::ByteView(payload)).value();
        auto view = Backend::MessageView::from(env);
        REQUIRE(view.has_value());

        auto res = router.onMessageReceived("sim_in", *view);
        REQUIRE(res.is<Backend::Forwarded>());
        CHECK(res.get<Backend::Forwarded>()->get().channel == "sim_admin");
    }

    SECTION("Admin rule with nested NOT fails when forbidden word is present")
    {
        Core::Net::UdpMetadata meta;
        meta.srcPort = htons(8080);
        meta.dstPort = htons(80);
        meta.srcIp = inet_addr("127.0.0.1");

        std::string payload = "HDR:Message with BAD content";
        auto env = Backend::MessageView::pack(meta, Common::Memory::ByteView(payload)).value();
        auto view = Backend::MessageView::from(env);
        REQUIRE(view.has_value());

        auto res = router.onMessageReceived("sim_in", *view);
        // NOT contains("BAD") failed, so this rule fails to match -> drops with 404
        REQUIRE(res.is<Backend::Dropped>());
        CHECK(res.get<Backend::Dropped>()->get().errorCode == 404);
    }
}

TEST_CASE("RouteHandler evaluates generic metadata field conditions and expr syntax", "[handler][router][metadata]")
{
    auto simBackend = std::make_unique<Core::Simulation::SimulationBackend>();
    auto& backend = *simBackend;

    Proxy::Handler::RouteHandler router(backend);

    Core::Config::Configuration config;
    config.set("route.input_channel", "sim_in");
    config.set("route.rules", "rule_meta_expr,rule_meta_config");

    // Rule 1: expr using metadata.<field> syntax
    config.set("route.rule.rule_meta_expr.expr", "metadata.src_port == 7777 && prefix(\"SECURE:\")");
    config.set("route.rule.rule_meta_expr.target", "sim_secure");

    // Rule 2: structured metadata field config
    config.set("route.rule.rule_meta_config.type", "metadata_field");
    config.set("route.rule.rule_meta_config.field", "address_family");
    config.set("route.rule.rule_meta_config.op", "==");
    config.set("route.rule.rule_meta_config.value", "4");
    config.set("route.rule.rule_meta_config.target", "sim_ipv4");

    REQUIRE(router.configure(config));

    SECTION("Rule matches metadata.src_port and prefix in expr")
    {
        Core::Net::UdpMetadata meta;
        meta.srcPort = 7777; // matches metadata.src_port == 7777
        meta.addressFamily = 4;

        std::string payload = "SECURE:TopSecretData";
        auto env = Backend::MessageView::pack(meta, Common::Memory::ByteView(payload)).value();
        auto view = Backend::MessageView::from(env);
        REQUIRE(view.has_value());

        auto res = router.onMessageReceived("sim_in", *view);
        REQUIRE(res.is<Backend::Forwarded>());
        CHECK(res.get<Backend::Forwarded>()->get().channel == "sim_secure");
    }

    SECTION("Rule falls back to metadata_field address_family")
    {
        Core::Net::UdpMetadata meta;
        meta.srcPort = 5555; // does not match rule 1
        meta.addressFamily = 4;

        std::string payload = "NORMAL:Data";
        auto env = Backend::MessageView::pack(meta, Common::Memory::ByteView(payload)).value();
        auto view = Backend::MessageView::from(env);
        REQUIRE(view.has_value());

        auto res = router.onMessageReceived("sim_in", *view);
        REQUIRE(res.is<Backend::Forwarded>());
        CHECK(res.get<Backend::Forwarded>()->get().channel == "sim_ipv4");
    }

    SECTION("MetadataFieldCondition fluent builder")
    {
        auto rule = Proxy::Handler::RouteRule::when(
            Proxy::Handler::MetadataField::equals("src_port", 9999))
                        .forwardTo("sim_special");

        Core::Net::UdpMetadata metaMatch;
        metaMatch.srcPort = 9999;
        auto envMatch = Backend::MessageView::pack(metaMatch, Common::Memory::ByteView("payload")).value();
        auto viewMatch = Backend::MessageView::from(envMatch);
        REQUIRE(viewMatch.has_value());
        CHECK(rule.matches(*viewMatch));

        Core::Net::UdpMetadata metaNoMatch;
        metaNoMatch.srcPort = 1111;
        auto envNoMatch = Backend::MessageView::pack(metaNoMatch, Common::Memory::ByteView("payload")).value();
        auto viewNoMatch = Backend::MessageView::from(envNoMatch);
        REQUIRE(viewNoMatch.has_value());
        CHECK_FALSE(rule.matches(*viewNoMatch));
    }
}

#if defined(PROXY_HAS_LUA) && PROXY_HAS_LUA
TEST_CASE("RouteHandler evaluates embedded Lua routing rules and rejections", "[handler][router][lua]")
{
    Core::Simulation::SimulationBackend backend;
    Proxy::Handler::RouteHandler router(backend);

    Core::Config::Configuration config;
    config.set("route.input_channel", "sim_in");
    config.set("route.rules", "rule_lua_reject,rule_lua_forward");

    // Lua Rule 1: Reject if payload contains '\n' more than twice
    config.set("route.rule.rule_lua_reject.type", "lua");
    config.set("route.rule.rule_lua_reject.script",
        "local _, count = string.gsub(msg.payload, '\\n', '')\n"
        "return count >= 3\n");
    config.set("route.rule.rule_lua_reject.action", "reject");
    config.set("route.rule.rule_lua_reject.reason", "Lua detected too many newlines");
    config.set("route.rule.rule_lua_reject.error_code", "429");

    // Lua Rule 2: Forward if client port is 7777 and payload starts with "LUA:"
    config.set("route.rule.rule_lua_forward.type", "lua");
    config.set("route.rule.rule_lua_forward.script",
        "return msg.port == 7777 and string.sub(msg.payload, 1, 4) == 'LUA:'\n");
    config.set("route.rule.rule_lua_forward.target", "sim_lua_out");
    config.set("route.rule.rule_lua_forward.action", "forward");

    REQUIRE(router.onConfigure(config));
    REQUIRE(router.rules().size() == 2);

    SECTION("Lua rule rejects when newline count matches")
    {
        std::string payload = "line1\nline2\nline3\nline4";
        auto env = Backend::MessageView::packRaw(Common::Memory::ByteView(payload)).value();
        auto view = Backend::MessageView::from(env);
        REQUIRE(view.has_value());

        auto res = router.onMessageReceived("sim_in", *view);
        REQUIRE(res.is<Backend::Dropped>());
        auto dropped = res.get<Backend::Dropped>();
        REQUIRE(dropped.has_value());
        CHECK(dropped->get().reason == "Lua detected too many newlines");
        CHECK(dropped->get().errorCode == 429);
    }

    SECTION("Lua rule forwards on port and prefix match")
    {
        Core::Net::UdpMetadata meta;
        meta.srcPort = htons(7777);
        meta.dstPort = htons(80);
        meta.srcIp = inet_addr("127.0.0.1");

        std::string payload = "LUA:Dynamic route packet";
        auto env = Backend::MessageView::pack(meta, Common::Memory::ByteView(payload)).value();
        auto view = Backend::MessageView::from(env);
        REQUIRE(view.has_value());

        auto res = router.onMessageReceived("sim_in", *view);
        REQUIRE(res.is<Backend::Forwarded>());
        auto fwd = res.get<Backend::Forwarded>();
        REQUIRE(fwd.has_value());
        CHECK(fwd->get().channel == "sim_lua_out");
    }

    SECTION("Lua rule falls through on port mismatch")
    {
        Core::Net::UdpMetadata meta;
        meta.srcPort = htons(8888); // not 7777
        meta.dstPort = htons(80);
        meta.srcIp = inet_addr("127.0.0.1");

        std::string payload = "LUA:Dynamic route packet";
        auto env = Backend::MessageView::pack(meta, Common::Memory::ByteView(payload)).value();
        auto view = Backend::MessageView::from(env);
        REQUIRE(view.has_value());

        auto res = router.onMessageReceived("sim_in", *view);
        REQUIRE(res.is<Backend::Dropped>());
        CHECK(res.get<Backend::Dropped>()->get().errorCode == 404);
    }
}
#endif
