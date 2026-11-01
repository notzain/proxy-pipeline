#include "common/compat/Jthread.hpp"
#include "common/memory/ObjectPool.hpp"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <thread>
#include <vector>

namespace {

struct TestNode {
    int id { 0 };
    std::string data;
};

} // namespace

TEST_CASE("Common::Memory::ObjectPool basic acquire and automatic handle recycling", "[memory]")
{
    Common::Memory::ObjectPool<TestNode> pool(4, /*allowOverflow=*/false);

    CHECK(pool.capacity() == 4);
    CHECK(pool.available() == 4);

    auto h1 = pool.acquire();
    auto h2 = pool.acquire();
    auto h3 = pool.acquire();
    auto h4 = pool.acquire();

    REQUIRE(h1);
    REQUIRE(h2);
    REQUIRE(h3);
    REQUIRE(h4);

    h1->id = 1;
    h2->id = 2;

    CHECK(pool.available() == 0);
    CHECK_FALSE(pool.acquire()); // Exhausted without overflow

    // Handle goes out of scope / is reset -> automatically recycled to pool!
    h1.reset();
    CHECK(pool.available() == 1);

    auto h5 = pool.acquire();
    REQUIRE(h5);
    CHECK(pool.available() == 0);
}

TEST_CASE("Common::Memory::ObjectPool elastic burst overflow and handle release", "[memory]")
{
    Common::Memory::ObjectPool<TestNode> pool(2, /*allowOverflow=*/true);

    auto h1 = pool.acquire();
    auto h2 = pool.acquire();
    REQUIRE(h1);
    REQUIRE(h2);
    CHECK(pool.available() == 0);

    // 3rd acquire triggers dynamic burst overflow
    auto h3 = pool.acquire();
    REQUIRE(h3);
    h3->id = 999;

    // Test detaching pointer from handle
    TestNode* detached = h3.release();
    REQUIRE(detached != nullptr);
    CHECK_FALSE(h3); // Disarmed

    // Manual recycle via pool
    pool.recycleRaw(detached);

    // h1 and h2 destruct here and return to pool
    h1.reset();
    h2.reset();

    CHECK(pool.available() == 2);
}

TEST_CASE("Common::Memory::ObjectPool concurrent churn with handles", "[memory]")
{
    constexpr std::size_t Threads = 4;
    constexpr std::size_t Iterations = 2000;
    Common::Memory::ObjectPool<TestNode> pool(16, /*allowOverflow=*/false);

    std::vector<Common::Compat::jthread> workers;
    for (std::size_t t = 0; t < Threads; ++t) {
        workers.emplace_back([&pool]() {
            for (std::size_t i = 0; i < Iterations; ++i) {
                auto handle = pool.acquire();
                if (handle) {
                    handle->id = static_cast<int>(i);
                    handle->data = "test_payload";
                    // handle destructs at loop end and recycles automatically!
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    workers.clear();
    CHECK(pool.available() == 16);
}
