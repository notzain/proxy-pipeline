#include "core/serialization/Serializer.hpp"
#include "core/serialization/TypeTraits.hpp"
#include "core/serialization/ir/Converter.hpp"
#include "core/serialization/ir/Value.hpp"
#include "core/serialization/json/JsonSerializer.hpp"

#if defined(PROXY_HAS_PUGIXML) && PROXY_HAS_PUGIXML
#include "core/serialization/xml/XmlSerializer.hpp"
#endif

#if defined(PROXY_HAS_XERCES) && PROXY_HAS_XERCES
#include "core/serialization/xml/XercesFormat.hpp"
#include "core/serialization/xml/XercesSerializer.hpp"
#endif

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

// 1. Simple POD without inheritance or macros
struct Point {
    int x;
    int y;

    bool operator==(const Point& other) const noexcept
    {
        return x == other.x && y == other.y;
    }
};

// 2. Nested POD
struct Circle {
    Point center;
    double radius;

    bool operator==(const Circle& other) const noexcept
    {
        return center == other.center && radius == other.radius;
    }
};

// 3. Complex Model with standard containers
struct ComplexModel {
    std::string id;
    std::optional<std::string> description;
    std::vector<int> numbers;
    std::array<std::string, 2> flags;
    std::map<std::string, int> scores;
    Circle boundary;

    bool operator==(const ComplexModel& other) const noexcept
    {
        return id == other.id && description == other.description && numbers == other.numbers && flags == other.flags && scores == other.scores && boundary == other.boundary;
    }
};

// 4. Model with XML attributes and child elements
struct UserProfile {
    // XML attributes
    std::uint32_t id;
    std::string role;
    std::optional<bool> active;

    // XML child elements
    std::string username;
    std::string email;
    Point location;

    bool operator==(const UserProfile& other) const noexcept
    {
        return id == other.id && role == other.role && active == other.active && username == other.username && email == other.email && location == other.location;
    }
};

} // namespace

template <>
struct Core::Serialization::TypeTraits<Point> {
    static constexpr auto fields()
    {
        return std::make_tuple(
            Core::Serialization::field("x", &Point::x),
            Core::Serialization::field("y", &Point::y));
    }
};

template <>
struct Core::Serialization::TypeTraits<Circle> {
    static constexpr auto fields()
    {
        return std::make_tuple(
            Core::Serialization::field("center", &Circle::center),
            Core::Serialization::field("radius", &Circle::radius));
    }
};

template <>
struct Core::Serialization::TypeTraits<ComplexModel> {
    static constexpr auto fields()
    {
        return std::make_tuple(
            Core::Serialization::field("id", &ComplexModel::id),
            Core::Serialization::field("description", &ComplexModel::description),
            Core::Serialization::field("numbers", &ComplexModel::numbers),
            Core::Serialization::field("flags", &ComplexModel::flags),
            Core::Serialization::field("scores", &ComplexModel::scores),
            Core::Serialization::field("boundary", &ComplexModel::boundary));
    }
};

template <>
struct Core::Serialization::TypeTraits<UserProfile> {
    static constexpr auto attributes()
    {
        return std::make_tuple(
            Core::Serialization::attribute("id", &UserProfile::id),
            Core::Serialization::attribute("role", &UserProfile::role),
            Core::Serialization::attribute("active", &UserProfile::active));
    }

    static constexpr auto fields()
    {
        return std::make_tuple(
            Core::Serialization::field("username", &UserProfile::username),
            Core::Serialization::field("email", &UserProfile::email),
            Core::Serialization::field("location", &UserProfile::location));
    }
};

TEST_CASE("JsonSerializer and JsonDeserializer with simple POD", "[serialization][json]")
{
    Point p { .x = 10, .y = -25 };

    std::string json = Core::Serialization::Json::JsonSerializer<Point>::serialize(p);
    REQUIRE(!json.empty());

    auto restored = Core::Serialization::Json::JsonDeserializer<Point>::deserialize(json);
    REQUIRE(restored.has_value());
    CHECK(restored->x == 10);
    CHECK(restored->y == -25);
    CHECK(*restored == p);
}

TEST_CASE("JsonSerializer and JsonDeserializer with nested PODs", "[serialization][json]")
{
    Circle c {
        .center = { .x = 5, .y = 12 },
        .radius = 3.14159
    };

    std::string json = Core::Serialization::Json::JsonSerializer<Circle>::serialize(c, { .indent = 2 });
    REQUIRE(!json.empty());

    auto restored = Core::Serialization::Json::JsonDeserializer<Circle>::deserialize(json);
    REQUIRE(restored.has_value());
    CHECK(restored->center.x == 5);
    CHECK(restored->center.y == 12);
    CHECK(restored->radius == 3.14159);
    CHECK(*restored == c);
}

TEST_CASE("JsonSerializer and JsonDeserializer with std containers and optionals", "[serialization][json]")
{
    SECTION("With optional value present")
    {
        ComplexModel model {
            .id = "model-001",
            .description = "Test description",
            .numbers = { 1, 2, 3, 5, 8 },
            .flags = { "alpha", "beta" },
            .scores = { { "player1", 100 }, { "player2", 200 } },
            .boundary = {
                .center = { .x = 100, .y = 200 },
                .radius = 50.0 }
        };

        std::string json = Core::Serialization::Json::JsonSerializer<ComplexModel>::serialize(model, { .indent = 2 });
        REQUIRE(!json.empty());

        auto restored = Core::Serialization::Json::JsonDeserializer<ComplexModel>::deserialize(json);
        REQUIRE(restored.has_value());
        CHECK(*restored == model);
        CHECK(restored->description.has_value());
        CHECK(*restored->description == "Test description");
        CHECK(restored->numbers.size() == 5);
        CHECK(restored->flags[0] == "alpha");
        CHECK(restored->scores.at("player1") == 100);
    }

    SECTION("With optional value nullopt")
    {
        ComplexModel model {
            .id = "model-002",
            .description = std::nullopt,
            .numbers = { },
            .flags = { "none", "active" },
            .scores = { },
            .boundary = {
                .center = { .x = 0, .y = 0 },
                .radius = 1.0 }
        };

        std::string json = Core::Serialization::Json::JsonSerializer<ComplexModel>::serialize(model);
        REQUIRE(!json.empty());

        auto restored = Core::Serialization::Json::JsonDeserializer<ComplexModel>::deserialize(json);
        REQUIRE(restored.has_value());
        CHECK(*restored == model);
        CHECK_FALSE(restored->description.has_value());
        CHECK(restored->numbers.empty());
        CHECK(restored->scores.empty());
    }
}

TEST_CASE("JsonDeserializer error handling on invalid input", "[serialization][json]")
{
    SECTION("Invalid JSON syntax")
    {
        auto restored = Core::Serialization::Json::JsonDeserializer<Point>::deserialize("{ not valid json }");
        CHECK_FALSE(restored.has_value());
    }

    SECTION("Mismatched data types")
    {
        auto restored = Core::Serialization::Json::JsonDeserializer<Point>::deserialize(R"({"x": "not a number", "y": 20})");
        CHECK_FALSE(restored.has_value());
    }

    SECTION("Missing required fields")
    {
        auto restored = Core::Serialization::Json::JsonDeserializer<Point>::deserialize(R"({"x": 10})");
        CHECK_FALSE(restored.has_value());
    }
}

#if defined(PROXY_HAS_PUGIXML) && PROXY_HAS_PUGIXML
TEST_CASE("XmlSerializer and XmlDeserializer with elements and attributes", "[serialization][xml]")
{
    SECTION("Simple POD serialization and deserialization")
    {
        Point p { .x = 42, .y = 84 };
        std::string xml = Core::Serialization::Xml::XmlSerializer<Point>::serialize(p, { .rootName = "Point" });
        REQUIRE(!xml.empty());
        CHECK(xml.find("<Point") != std::string::npos);
        CHECK(xml.find("<x>42</x>") != std::string::npos);
        CHECK(xml.find("<y>84</y>") != std::string::npos);

        auto restored = Core::Serialization::Xml::XmlDeserializer<Point>::deserialize(xml, { .rootName = "Point" });
        REQUIRE(restored.has_value());
        CHECK(restored->x == 42);
        CHECK(restored->y == 84);
        CHECK(*restored == p);
    }

    SECTION("Model with XML attributes and nested elements")
    {
        UserProfile profile {
            .id = 1337,
            .role = "admin",
            .active = true,
            .username = "johndoe",
            .email = "john@example.com",
            .location = { .x = 10, .y = 20 }
        };

        std::string xml = Core::Serialization::Xml::XmlSerializer<UserProfile>::serialize(profile, { .rootName = "UserProfile" });
        REQUIRE(!xml.empty());

        // Validate that attributes are rendered inside tag
        CHECK(xml.find(R"(id="1337")") != std::string::npos);
        CHECK(xml.find(R"(role="admin")") != std::string::npos);
        CHECK(xml.find(R"(active="true")") != std::string::npos);

        // Validate that fields are rendered as child elements
        CHECK(xml.find("<username>johndoe</username>") != std::string::npos);
        CHECK(xml.find("<email>john@example.com</email>") != std::string::npos);
        CHECK(xml.find("<location>") != std::string::npos);
        CHECK(xml.find("<x>10</x>") != std::string::npos);
        CHECK(xml.find("<y>20</y>") != std::string::npos);

        // Deserialize back
        auto restored = Core::Serialization::Xml::XmlDeserializer<UserProfile>::deserialize(xml, { .rootName = "UserProfile" });
        REQUIRE(restored.has_value());
        CHECK(restored->id == 1337);
        CHECK(restored->role == "admin");
        CHECK(restored->active == true);
        CHECK(restored->username == "johndoe");
        CHECK(restored->email == "john@example.com");
        CHECK(restored->location.x == 10);
        CHECK(restored->location.y == 20);
        CHECK(*restored == profile);
    }

    SECTION("XML error handling on invalid syntax")
    {
        auto restored = Core::Serialization::Xml::XmlDeserializer<Point>::deserialize("<Point><x>10</Point>");
        CHECK_FALSE(restored.has_value());
    }
}
#endif

#if defined(PROXY_HAS_XERCES) && PROXY_HAS_XERCES
TEST_CASE("XercesSerializer and XercesDeserializer with elements and attributes", "[serialization][xml][xerces]")
{
    SECTION("Simple POD serialization and deserialization with Xerces-C")
    {
        Point p { .x = 42, .y = 84 };
        std::string xml = Core::Serialization::Xml::XercesSerializer<Point>::serialize(p, { .rootName = "Point" });
        REQUIRE(!xml.empty());
        CHECK(xml.find("<Point") != std::string::npos);
        CHECK(xml.find("<x>42</x>") != std::string::npos);
        CHECK(xml.find("<y>84</y>") != std::string::npos);

        auto restored = Core::Serialization::Xml::XercesDeserializer<Point>::deserialize(xml, { .rootName = "Point" });
        REQUIRE(restored.has_value());
        CHECK(restored->x == 42);
        CHECK(restored->y == 84);
        CHECK(*restored == p);
    }

    SECTION("Model with XML attributes and nested elements with Xerces-C")
    {
        UserProfile profile {
            .id = 1337,
            .role = "admin",
            .active = true,
            .username = "johndoe",
            .email = "john@example.com",
            .location = { .x = 10, .y = 20 }
        };

        std::string xml = Core::Serialization::Xml::XercesSerializer<UserProfile>::serialize(profile, { .rootName = "UserProfile" });
        REQUIRE(!xml.empty());

        CHECK(xml.find(R"(id="1337")") != std::string::npos);
        CHECK(xml.find(R"(role="admin")") != std::string::npos);
        CHECK(xml.find(R"(active="true")") != std::string::npos);

        CHECK(xml.find("<username>johndoe</username>") != std::string::npos);
        CHECK(xml.find("<email>john@example.com</email>") != std::string::npos);
        CHECK(xml.find("<location>") != std::string::npos);
        CHECK(xml.find("<x>10</x>") != std::string::npos);
        CHECK(xml.find("<y>20</y>") != std::string::npos);

        auto restored = Core::Serialization::Xml::XercesDeserializer<UserProfile>::deserialize(xml, { .rootName = "UserProfile" });
        REQUIRE(restored.has_value());
        CHECK(restored->id == 1337);
        CHECK(restored->role == "admin");
        CHECK(restored->active == true);
        CHECK(restored->username == "johndoe");
        CHECK(restored->email == "john@example.com");
        CHECK(restored->location.x == 10);
        CHECK(restored->location.y == 20);
        CHECK(*restored == profile);
    }

    SECTION("Xerces XML error handling on invalid syntax")
    {
        auto restored = Core::Serialization::Xml::XercesDeserializer<Point>::deserialize("<Point><x>10</Point>");
        CHECK_FALSE(restored.has_value());
    }
}
#endif

TEST_CASE("Intermediate representation (IR) value trees and generic facade", "[serialization][ir]")
{
    Point p { .x = 42, .y = 99 };
    Core::Serialization::Ir::Value ir = Core::Serialization::Ir::toIntermediate(p);
    REQUIRE(ir.isObject());
    REQUIRE(ir.findField("x") != nullptr);
    CHECK(ir.findField("x")->asInt() == 42);
    REQUIRE(ir.findField("y") != nullptr);
    CHECK(ir.findField("y")->asInt() == 99);

    auto restored = Core::Serialization::Ir::fromIntermediate<Point>(ir);
    REQUIRE(restored.has_value());
    CHECK(restored->x == 42);
    CHECK(restored->y == 99);

    std::string json = Core::Serialization::serialize<Core::Serialization::Json::JsonFormat>(p);
    auto fromJson = Core::Serialization::deserialize<Core::Serialization::Json::JsonFormat, Point>(json);
    REQUIRE(fromJson.has_value());
    CHECK(*fromJson == p);

#if defined(PROXY_HAS_PUGIXML) && PROXY_HAS_PUGIXML
    std::string xml = Core::Serialization::serialize<Core::Serialization::Xml::XmlFormat>(p, { .rootName = "Point" });
    auto fromXml = Core::Serialization::deserialize<Core::Serialization::Xml::XmlFormat, Point>(xml, { .rootName = "Point" });
    REQUIRE(fromXml.has_value());
    CHECK(*fromXml == p);
#endif

#if defined(PROXY_HAS_XERCES) && PROXY_HAS_XERCES
    std::string xercesXml = Core::Serialization::serialize<Core::Serialization::Xml::XercesFormat>(p, { .rootName = "Point" });
    auto fromXerces = Core::Serialization::deserialize<Core::Serialization::Xml::XercesFormat, Point>(xercesXml, { .rootName = "Point" });
    REQUIRE(fromXerces.has_value());
    CHECK(*fromXerces == p);
#endif
}
