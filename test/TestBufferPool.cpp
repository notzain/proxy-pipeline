#include "core/memory/BufferPool.hpp"
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

TEST_CASE("Core::Memory::BufferPool allocation and release", "[memory]")
{
    auto pool = std::make_shared<Core::Memory::BufferPool>(1024, 4);

    CHECK(pool->available() == 4);

    auto b1 = pool->acquire();
    auto b2 = pool->acquire();
    auto b3 = pool->acquire();
    auto b4 = pool->acquire();

    REQUIRE(b1.has_value());
    REQUIRE(b2.has_value());
    REQUIRE(b3.has_value());
    REQUIRE(b4.has_value());

    CHECK(pool->available() == 0);
    CHECK_FALSE(pool->acquire().has_value()); // Exhausted

    b1.reset();
    CHECK(pool->available() == 1);

    auto b5 = pool->acquire();
    REQUIRE(b5.has_value());
    CHECK(pool->available() == 0);
}

TEST_CASE("Core::Memory::BufferPool multi-threaded concurrent churn", "[memory]")
{
    constexpr std::size_t Threads = 4;
    constexpr std::size_t Iterations = 5000;
    auto pool = std::make_shared<Core::Memory::BufferPool>(256, 16);

    std::vector<std::jthread> workers;
    for (std::size_t t = 0; t < Threads; ++t) {
        workers.emplace_back([pool]() {
            for (std::size_t i = 0; i < Iterations; ++i) {
                auto slice = pool->acquire();
                if (slice) {
                    slice->span()[0] = static_cast<std::byte>(i & 0xFF);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    workers.clear();
    CHECK(pool->available() == 16);
}

