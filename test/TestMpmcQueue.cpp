#include "common/compat/Jthread.hpp"
#include "common/queue/MpmcQueue.hpp"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <numeric>
#include <set>
#include <thread>
#include <vector>

TEST_CASE("Common::Queue::MpmcQueue basic push and pop", "[mpmc]")
{
    Common::Queue::MpmcQueue<int, 4> q;

    CHECK(q.empty());
    CHECK(q.size() == 0);

    CHECK(q.tryPush(1));
    CHECK(q.tryPush(2));
    CHECK(q.tryPush(3));
    CHECK(q.tryPush(4));
    CHECK_FALSE(q.tryPush(5)); // Queue full

    int val = 0;
    CHECK(q.tryPop(val));
    CHECK(val == 1);
    CHECK(q.tryPop(val));
    CHECK(val == 2);
    CHECK(q.tryPop(val));
    CHECK(val == 3);
    CHECK(q.tryPop(val));
    CHECK(val == 4);
    CHECK_FALSE(q.tryPop(val)); // Queue empty
}

TEST_CASE("Common::Queue::MpmcQueue preserves FIFO order across wrap-around", "[mpmc]")
{
    Common::Queue::MpmcQueue<int, 8> q;

    for (int round = 0; round < 50; ++round) {
        for (int i = 0; i < 4; ++i) {
            REQUIRE(q.tryPush(round * 4 + i));
        }
        for (int i = 0; i < 4; ++i) {
            int val = -1;
            REQUIRE(q.tryPop(val));
            REQUIRE(val == round * 4 + i);
        }
    }
    CHECK(q.empty());
}

TEST_CASE("Common::Queue::MpmcQueue multi-producer multi-consumer concurrent stress", "[mpmc]")
{
    constexpr std::size_t NumProducers = 4;
    constexpr std::size_t NumConsumers = 4;
    constexpr std::size_t ItemsPerProducer = 10000;
    constexpr std::size_t TotalItems = NumProducers * ItemsPerProducer;

    Common::Queue::MpmcQueue<int, 1024> q;

    std::vector<Common::Compat::jthread> producers;
    producers.reserve(NumProducers);

    for (std::size_t p = 0; p < NumProducers; ++p) {
        producers.emplace_back([&q, p]() {
            for (std::size_t i = 0; i < ItemsPerProducer; ++i) {
                int item = static_cast<int>(p * ItemsPerProducer + i);
                while (!q.push(item)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    std::vector<std::vector<int>> consumerResults(NumConsumers);
    std::vector<Common::Compat::jthread> consumers;
    consumers.reserve(NumConsumers);

    std::atomic<std::size_t> totalPopped { 0 };

    for (std::size_t c = 0; c < NumConsumers; ++c) {
        consumers.emplace_back([&q, &consumerResults, &totalPopped, c]() {
            int item = 0;
            while (totalPopped.load(std::memory_order_relaxed) < TotalItems) {
                if (q.tryPop(item)) {
                    consumerResults[c].push_back(item);
                    totalPopped.fetch_add(1, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }

    producers.clear();
    consumers.clear();

    std::set<int> allReceived;
    for (const auto& res : consumerResults) {
        for (int v : res) {
            auto [_, inserted] = allReceived.insert(v);
            CHECK(inserted); // No duplicates
        }
    }

    CHECK(allReceived.size() == TotalItems);
}
