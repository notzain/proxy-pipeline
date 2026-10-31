#include "common/memory/BufferPool.hpp"
#include "common/memory/DynamicBuffer.hpp"
#include <catch2/catch_test_macros.hpp>
#include <vector>

TEST_CASE("Common::Memory::DynamicBuffer operations and variant semantics", "[common][memory][dynamic_buffer]")
{
    SECTION("Default constructed is empty monostate")
    {
        Common::Memory::DynamicBuffer buf;
        CHECK_FALSE(Common::Memory::hasData(buf));
        CHECK(Common::Memory::sizeOf(buf) == 0);
        CHECK(Common::Memory::spanOf(buf).empty());
    }

    SECTION("Holding std::vector<std::byte>")
    {
        std::vector<std::byte> vec { std::byte { 0xAA }, std::byte { 0xBB } };
        Common::Memory::DynamicBuffer buf = std::move(vec);

        REQUIRE(Common::Memory::hasData(buf));
        REQUIRE(buf.is<std::vector<std::byte>>());
        CHECK(Common::Memory::sizeOf(buf) == 2);

        auto span = Common::Memory::spanOf(buf);
        REQUIRE(span.size() == 2);
        CHECK(span[0] == std::byte { 0xAA });
        CHECK(span[1] == std::byte { 0xBB });

        auto mutSpan = Common::Memory::mutableSpanOf(buf);
        mutSpan[0] = std::byte { 0xCC };
        CHECK(Common::Memory::spanOf(buf)[0] == std::byte { 0xCC });
    }

    SECTION("Holding pooled BufferSlice and releasing block on reassignment")
    {
        auto pool = std::make_shared<Common::Memory::BufferPool>(64, 2);
        REQUIRE(pool->available() == 2);

        {
            auto slice = pool->acquire();
            REQUIRE(slice.has_value());
            slice->resize(16);
            CHECK(pool->available() == 1);

            Common::Memory::DynamicBuffer buf = std::move(*slice);
            REQUIRE(Common::Memory::hasData(buf));
            REQUIRE(buf.is<Common::Memory::BufferSlice>());
            CHECK(Common::Memory::sizeOf(buf) == 16);
            CHECK(pool->available() == 1);

            // Reassigning to vector or monostate automatically releases the BufferSlice block back to the pool
            buf = std::monostate { };
            CHECK_FALSE(Common::Memory::hasData(buf));
            CHECK(pool->available() == 2);
        }

        CHECK(pool->available() == 2);
    }
}
