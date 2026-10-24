#include "core/memory/SmallSpanBuffer.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <span>
#include <vector>

TEST_CASE("Core::Memory::SmallSpanBuffer inline stack capacity", "[memory][core]")
{
    Core::Memory::SmallSpanBuffer<int, 4> buffer(4);
    CHECK(buffer.capacity() == 4);

    for (int i = 0; i < 4; ++i) {
        buffer[i] = i * 10;
    }

    auto span = buffer.span(4);
    REQUIRE(span.size() == 4);
    CHECK(span[0] == 0);
    CHECK(span[1] == 10);
    CHECK(span[2] == 20);
    CHECK(span[3] == 30);
}

TEST_CASE("Core::Memory::SmallSpanBuffer heap spillover", "[memory][core]")
{
    Core::Memory::SmallSpanBuffer<int, 4> buffer(10);
    CHECK(buffer.capacity() == 10);

    for (int i = 0; i < 10; ++i) {
        buffer[i] = i * 5;
    }

    auto span = buffer.span(7);
    REQUIRE(span.size() == 7);
    CHECK(span[0] == 0);
    CHECK(span[6] == 30);
}
