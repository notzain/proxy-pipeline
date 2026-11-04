#include "backend/IConfig.hpp"
#include "core/config/ConfigReader.hpp"
#include "core/config/Configuration.hpp"
#include "core/serialization/TypeTraits.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace {

struct TestRule {
    std::string name;
    int priority { 0 };
    bool active { false };
};

} // namespace

template <>
struct Core::Serialization::TypeTraits<TestRule> {
    static constexpr bool isReflected = true;
    static constexpr auto fields()
    {
        return std::make_tuple(
            Core::Serialization::field("name", &TestRule::name),
            Core::Serialization::field("priority", &TestRule::priority),
            Core::Serialization::field("active", &TestRule::active));
    }
};

TEST_CASE("IConfig strongly-typed getters via Core::Config::Configuration", "[config]")
{
    Core::Config::Configuration config;
    config.set("app.name", "ProxyApp");
    config.set("server.port", "8080");
    config.set("server.timeout_ms", "-500");
    config.set("server.ratio", "3.14159");
    config.set("feature.enabled", "true");
    config.set("feature.disabled", "false");
    config.set("feature.yes", "yes");
    config.set("feature.no", "0");

    SECTION("String values")
    {
        CHECK(config.get<std::string>("app.name") == "ProxyApp");
        CHECK(config.getOr<std::string>("app.name", "default") == "ProxyApp");
        CHECK(config.getOr<std::string>("missing.key", "fallback") == "fallback");
        CHECK(config.has("app.name"));
        CHECK_FALSE(config.has("missing.key"));
    }

    SECTION("Integer values")
    {
        auto port = config.get<uint16_t>("server.port");
        REQUIRE(port.has_value());
        CHECK(*port == 8080);
        CHECK(config.getOr<uint16_t>("server.port", 9000) == 8080);
        CHECK(config.getOr<uint16_t>("missing.port", 9000) == 9000);

        auto timeout = config.get<int64_t>("server.timeout_ms");
        REQUIRE(timeout.has_value());
        CHECK(*timeout == -500);
    }

    SECTION("Floating point values")
    {
        auto ratio = config.get<double>("server.ratio");
        REQUIRE(ratio.has_value());
        CHECK_THAT(*ratio, Catch::Matchers::WithinRel(3.14159, 0.00001));
    }

    SECTION("Boolean values")
    {
        CHECK(config.get<bool>("feature.enabled") == true);
        CHECK(config.get<bool>("feature.disabled") == false);
        CHECK(config.get<bool>("feature.yes") == true);
        CHECK(config.get<bool>("feature.no") == false);
        CHECK(config.getOr<bool>("feature.missing", true) == true);
    }

    SECTION("Type conversion failure handling")
    {
        // String that cannot convert to integer
        auto badInt = config.get<int>("app.name");
        CHECK_FALSE(badInt.has_value());
        CHECK(config.getOr<int>("app.name", 42) == 42);

        // String that cannot convert to bool
        auto badBool = config.get<bool>("app.name");
        CHECK_FALSE(badBool.has_value());
    }
}

TEST_CASE("Hierarchical JSON configuration and navigation", "[config]")
{
    const std::string_view json = R"({
        "app": {
            "name": "ProxyApp",
            "version": 2
        },
        "servers": [
            { "host": "127.0.0.1", "port": 8080 },
            { "host": "10.0.0.1", "port": 9090 }
        ],
        "rules": [
            { "name": "ruleA", "priority": 10, "active": true },
            { "name": "ruleB", "priority": 20, "active": false }
        ],
        "tags": ["alpha", "beta", "gamma"]
    })";

    auto config = Core::Config::Configuration::fromJson(json);

    SECTION("Dot notation traversal for nested objects and arrays")
    {
        CHECK(config.get<std::string>("app.name") == "ProxyApp");
        CHECK(config.get<int>("app.version") == 2);
        CHECK(config.get<std::string>("servers.0.host") == "127.0.0.1");
        CHECK(config.get<int>("servers.0.port") == 8080);
        CHECK(config.get<std::string>("servers.1.host") == "10.0.0.1");
        CHECK(config.get<int>("servers.1.port") == 9090);
        CHECK(config.get<std::string>("tags.0") == "alpha");
        CHECK(config.get<std::string>("tags.2") == "gamma");
        CHECK_FALSE(config.get<std::string>("tags.3").has_value());
    }

    SECTION("Sub-node navigation via getNode()")
    {
        auto appNode = config.getNode("app");
        REQUIRE(appNode != nullptr);
        CHECK(appNode->get<std::string>("name") == "ProxyApp");
        CHECK(appNode->get<int>("version") == 2);

        auto s0 = config.getNode("servers.0");
        REQUIRE(s0 != nullptr);
        CHECK(s0->get<std::string>("host") == "127.0.0.1");
        CHECK(s0->get<int>("port") == 8080);
    }

    SECTION("Child iteration via getChildren()")
    {
        auto serverChildren = config.getChildren("servers");
        REQUIRE(serverChildren.size() == 2);
        CHECK(serverChildren[0]->get<std::string>("host") == "127.0.0.1");
        CHECK(serverChildren[1]->get<std::string>("host") == "10.0.0.1");

        auto tagChildren = config.getChildren("tags");
        REQUIRE(tagChildren.size() == 3);
        CHECK(tagChildren[0]->get<std::string>("") == "alpha");
        CHECK(tagChildren[1]->get<std::string>("") == "beta");
        CHECK(tagChildren[2]->get<std::string>("") == "gamma");
    }

    SECTION("Reflected struct and vector deserialization via Core::Config::get")
    {
        auto rule0 = Core::Config::get<TestRule>(config, "rules.0");
        REQUIRE(rule0.has_value());
        CHECK(rule0->name == "ruleA");
        CHECK(rule0->priority == 10);
        CHECK(rule0->active == true);

        auto rules = Core::Config::get<std::vector<TestRule>>(config, "rules");
        REQUIRE(rules.has_value());
        REQUIRE(rules->size() == 2);
        CHECK((*rules)[0].name == "ruleA");
        CHECK((*rules)[1].name == "ruleB");
        CHECK((*rules)[1].priority == 20);
        CHECK((*rules)[1].active == false);

        auto tags = Core::Config::get<std::vector<std::string>>(config, "tags");
        REQUIRE(tags.has_value());
        REQUIRE(tags->size() == 3);
        CHECK((*tags)[0] == "alpha");
        CHECK((*tags)[1] == "beta");
        CHECK((*tags)[2] == "gamma");
    }
}
