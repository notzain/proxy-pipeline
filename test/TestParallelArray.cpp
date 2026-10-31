#include "common/memory/ParallelArray.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>

TEST_CASE("Common::Memory::ParallelArray operations", "[common][memory][parallel_array]")
{
    SECTION("Empty array properties")
    {
        Common::Memory::ParallelArray<4, int, std::string> arr;
        REQUIRE(arr.empty());
        REQUIRE(arr.size() == 0);
        REQUIRE(arr.capacity() == 4);
        REQUIRE(arr.span<0>().empty());
        REQUIRE(arr.span<1>().empty());
    }

    SECTION("push_back keeps parallel columns in sync")
    {
        Common::Memory::ParallelArray<4, int, std::string, double> arr;
        arr.push_back(10, "alpha", 1.5);
        arr.push_back(20, "beta", 2.5);

        REQUIRE(arr.size() == 2);
        REQUIRE_FALSE(arr.empty());

        auto col0 = arr.span<0>();
        auto col1 = arr.span<1>();
        auto col2 = arr.span<2>();

        REQUIRE(col0.size() == 2);
        CHECK(col0[0] == 10);
        CHECK(col0[1] == 20);

        REQUIRE(col1.size() == 2);
        CHECK(col1[0] == "alpha");
        CHECK(col1[1] == "beta");

        REQUIRE(col2.size() == 2);
        CHECK(col2[0] == 1.5);
        CHECK(col2[1] == 2.5);
    }

    SECTION("Typed span access for distinct column types")
    {
        Common::Memory::ParallelArray<4, int, std::string> arr;
        arr.push_back(42, "hello");

        CHECK(arr.span<int>()[0] == 42);
        CHECK(arr.span<std::string>()[0] == "hello");
    }

    SECTION("get<I> element mutation and reference semantics")
    {
        Common::Memory::ParallelArray<4, int, std::string> arr;
        arr.push_back(1, "orig");

        arr.get<0>(0) = 99;
        arr.get<1>(0) = "updated";

        CHECK(arr.span<0>()[0] == 99);
        CHECK(arr.span<1>()[0] == "updated");
    }

    SECTION("clear resets active count")
    {
        Common::Memory::ParallelArray<4, int, std::string> arr;
        arr.push_back(1, "one");
        arr.push_back(2, "two");
        REQUIRE(arr.size() == 2);

        arr.clear();
        REQUIRE(arr.empty());
        REQUIRE(arr.size() == 0);
        REQUIRE(arr.span<0>().empty());

        arr.push_back(3, "three");
        REQUIRE(arr.size() == 1);
        CHECK(arr.span<0>()[0] == 3);
        CHECK(arr.span<1>()[0] == "three");
    }
}
