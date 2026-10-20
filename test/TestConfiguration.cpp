#include "backend/IConfig.hpp"
#include "core/config/Configuration.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

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
