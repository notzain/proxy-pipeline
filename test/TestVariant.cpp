#include "common/types/Variant.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

TEST_CASE("Common::Types::Variant basic operations and type queries", "[common][types][variant]")
{
    using MyVariant = Common::Types::Variant<int, std::string, double>;

    SECTION("Default construction")
    {
        MyVariant v;
        CHECK(v.is<int>());
        CHECK_FALSE(v.is<std::string>());
        CHECK_FALSE(v.is<double>());

        auto val = v.get<int>();
        REQUIRE(val.has_value());
        CHECK(val->get() == 0);
    }

    SECTION("Value construction and assignment")
    {
        MyVariant v = std::string("hello");
        CHECK(v.is<std::string>());
        CHECK_FALSE(v.is<int>());

        CHECK(v.get<int>() == std::nullopt);
        auto strOpt = v.get<std::string>();
        REQUIRE(strOpt.has_value());
        CHECK(strOpt->get() == "hello");

        // Mutate through returned reference
        strOpt->get() = "world";
        CHECK(v.get<std::string>()->get() == "world");

        v = 42;
        CHECK(v.is<int>());
        CHECK(v.get<int>()->get() == 42);
    }

    SECTION("Const Variant get<T>()")
    {
        const MyVariant v = 3.14;
        CHECK(v.is<double>());
        CHECK_FALSE(v.is<int>());

        auto opt = v.get<double>();
        REQUIRE(opt.has_value());
        CHECK(opt->get() == 3.14);
        CHECK(v.get<int>() == std::nullopt);
    }

    SECTION("Rvalue Variant get<T>() moves contained value")
    {
        MyVariant v = std::string("moveable payload");
        auto movedOpt = std::move(v).get<std::string>();
        REQUIRE(movedOpt.has_value());
        CHECK(*movedOpt == "moveable payload");
    }
}

TEST_CASE("Common::Types::Variant pattern matching via match()", "[common][types][variant]")
{
    using MyVariant = Common::Types::Variant<int, std::string, double>;

    SECTION("Matching with return value")
    {
        MyVariant v = 100;
        int result = v.match(
            [](int i) { return i * 2; },
            [](const std::string& s) { return static_cast<int>(s.length()); },
            [](double d) { return static_cast<int>(d); });
        CHECK(result == 200);

        v = std::string("test");
        result = v.match(
            [](int i) { return i * 2; },
            [](const std::string& s) { return static_cast<int>(s.length()); },
            [](double d) { return static_cast<int>(d); });
        CHECK(result == 4);
    }

    SECTION("Matching on const variant")
    {
        const MyVariant v = 2.5;
        std::string desc = v.match(
            [](int) { return "int"; },
            [](const std::string&) { return "string"; },
            [](double) { return "double"; });
        CHECK(desc == "double");
    }

    SECTION("Matching with mutation (in-place)")
    {
        MyVariant v = 10;
        v.match(
            [](int& i) { i += 5; },
            [](std::string& s) { s += "!"; },
            [](double& d) { d += 1.0; });
        CHECK(v.get<int>()->get() == 15);
    }

    SECTION("Matching with rvalue move")
    {
        struct MoveOnly {
            std::string text;
            MoveOnly(std::string t)
                : text(std::move(t))
            {
            }
            MoveOnly(const MoveOnly&) = delete;
            MoveOnly& operator=(const MoveOnly&) = delete;
            MoveOnly(MoveOnly&&) noexcept = default;
            MoveOnly& operator=(MoveOnly&&) noexcept = default;
        };

        Common::Types::Variant<int, MoveOnly> mv = MoveOnly { "exclusive" };
        std::string extracted = std::move(mv).match(
            [](int) { return std::string("int"); },
            [](MoveOnly&& m) { return std::move(m.text); });
        CHECK(extracted == "exclusive");
    }

    SECTION("Interop with std::variant functions")
    {
        MyVariant v = 42;
        CHECK(std::holds_alternative<int>(v));
        CHECK(std::get<int>(v) == 42);
        CHECK(v.index() == 0);
    }
}

