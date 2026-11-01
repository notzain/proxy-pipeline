#include "core/config/ConfigItem.hpp"
#include "core/config/ConfigSection.hpp"
#include "core/config/Configuration.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

namespace {

struct SampleNetworkConfig : public Core::Config::ConfigSection {
    Core::Config::ConfigItem<std::string> host { this, "network.host", "127.0.0.1" };
    Core::Config::ConfigItem<int> port { this, "network.port", 8080 };
    Core::Config::ConfigItem<bool> enabled { this, "network.enabled", true };
    Core::Config::ConfigItem<double> timeoutSec { this, "network.timeout_sec", 2.5 };
};

} // namespace

TEST_CASE("ConfigSection and ConfigItem basic loading and defaults", "[config]")
{
    SECTION("Initializes with default values")
    {
        SampleNetworkConfig cfg;
        CHECK(cfg.host.value() == "127.0.0.1");
        CHECK(cfg.port.value() == 8080);
        CHECK(cfg.enabled.value() == true);
        CHECK_THAT(cfg.timeoutSec.value(), Catch::Matchers::WithinRel(2.5, 0.001));

        // Test implicit conversion operator
        std::string hostStr = cfg.host;
        CHECK(hostStr == "127.0.0.1");
    }

    SECTION("Loads values from Configuration correctly")
    {
        Core::Config::Configuration config;
        config.set("network.host", "10.0.0.1");
        config.set("network.port", "9090");
        config.set("network.enabled", "false");
        config.set("network.timeout_sec", "5.0");

        SampleNetworkConfig cfg;
        REQUIRE(cfg.load(config));

        CHECK(cfg.host.value() == "10.0.0.1");
        CHECK(cfg.port.value() == 9090);
        CHECK(cfg.enabled.value() == false);
        CHECK_THAT(cfg.timeoutSec.value(), Catch::Matchers::WithinRel(5.0, 0.001));
    }

    SECTION("Partial configuration retains defaults for missing keys")
    {
        Core::Config::Configuration config;
        config.set("network.port", "443");

        SampleNetworkConfig cfg;
        REQUIRE(cfg.load(config));

        CHECK(cfg.host.value() == "127.0.0.1");
        CHECK(cfg.port.value() == 443);
        CHECK(cfg.enabled.value() == true);
    }

    SECTION("Handles invalid type gracefully by returning false")
    {
        Core::Config::Configuration config;
        config.set("network.port", "not_an_integer");

        SampleNetworkConfig cfg;
        CHECK_FALSE(cfg.load(config));
        // Value remains the default when type conversion fails
        CHECK(cfg.port.value() == 8080);
    }

    SECTION("Exports to Intermediate Representation (IR Object)")
    {
        Core::Config::Configuration config;
        config.set("network.host", "192.168.1.1");
        config.set("network.port", "3000");

        SampleNetworkConfig cfg;
        REQUIRE(cfg.load(config));

        auto irVal = cfg.toIr();
        REQUIRE(irVal.isObject());

        const auto* obj = irVal.asObject();
        REQUIRE(obj != nullptr);
        REQUIRE(obj->find("network.host") != obj->end());
        CHECK(obj->at("network.host").asStringView() == "192.168.1.1");

        REQUIRE(obj->find("network.port") != obj->end());
        CHECK(obj->at("network.port").asInt() == 3000);

        REQUIRE(obj->find("network.enabled") != obj->end());
        CHECK(obj->at("network.enabled").asBool() == true);
    }
}
