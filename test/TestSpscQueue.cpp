#include "common/compat/Jthread.hpp"
#include "common/compat/Span.hpp"
#include "common/compat/StopToken.hpp"
#include "common/queue/SpscQueue.hpp"
#include "core/threading/MessageTask.hpp"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstring>
#include <numeric>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace {

template <typename Queue>
std::vector<int> drainAll(Queue& q, std::size_t expected)
{
    std::vector<int> out;
    while (out.size() < expected) {
        q.drainContiguous(64, [&](Common::Compat::span<const int> s) { out.insert(out.end(), s.begin(), s.end()); });
    }
    return out;
}

} // namespace

TEST_CASE("Common::Queue::SpscQueue preserves FIFO order across many wrap-arounds", "[spsc]")
{
    Common::Queue::SpscQueue<int, 8> q;
    int next = 0;
    std::vector<int> received;
    for (int round = 0; round < 100; ++round) {
        REQUIRE(q.pushBatch(5, [&](int& slot, std::size_t) { slot = next++; }));
        auto got = drainAll(q, 5);
        received.insert(received.end(), got.begin(), got.end());
    }
    std::vector<int> expected(500);
    std::iota(expected.begin(), expected.end(), 0);
    CHECK(received == expected);
    CHECK(q.empty());
}

TEST_CASE("Common::Queue::SpscQueue drainContiguous splits at the physical ring boundary", "[spsc]")
{
    Common::Queue::SpscQueue<int, 4> q;
    REQUIRE(q.pushBatch(3, [](int& s, std::size_t i) { s = static_cast<int>(i); }));
    drainAll(q, 3); // head = tail = 3

    REQUIRE(q.pushBatch(4, [](int& s, std::size_t i) { s = 10 + static_cast<int>(i); }));
    REQUIRE(q.size() == 4);

    std::vector<std::size_t> sliceSizes;
    std::vector<int> values;
    while (!q.empty()) {
        q.drainContiguous(16, [&](Common::Compat::span<const int> s) {
            sliceSizes.push_back(s.size());
            values.insert(values.end(), s.begin(), s.end());
        });
    }
    CHECK(sliceSizes == std::vector<std::size_t> { 1, 3 });
    CHECK(values == std::vector<int> { 10, 11, 12, 13 });
}

TEST_CASE("Common::Queue::SpscQueue minimum power-of-two capacity (2) works at the mask boundary", "[spsc]")
{
    Common::Queue::SpscQueue<int, 2> q;
    STATIC_REQUIRE(decltype(q)::Mask == 1);
    for (int i = 0; i < 50; ++i) {
        REQUIRE(q.pushBatch(2, [&](int& s, std::size_t k) { s = i * 2 + static_cast<int>(k); }));
        auto got = drainAll(q, 2);
        CHECK(got == std::vector<int> { i * 2, i * 2 + 1 });
    }
}

TEST_CASE("Common::Queue::SpscQueue rejects invalid batch sizes", "[spsc]")
{
    Common::Queue::SpscQueue<int, 4> q;
    CHECK_FALSE(q.pushBatch(0, [](int&, std::size_t) { }));
    CHECK_FALSE(q.pushBatch(5, [](int&, std::size_t) { }));
    CHECK(q.empty());
}

TEST_CASE("Common::Queue::SpscQueue producer blocks when full and resumes after drain", "[spsc]")
{
    Common::Queue::SpscQueue<int, 4> q;
    REQUIRE(q.pushBatch(4, [](int& s, std::size_t i) { s = static_cast<int>(i); }));

    std::atomic<bool> pushed { false };
    Common::Compat::jthread producer([&] {
        q.pushBatch(2, [](int& s, std::size_t i) { s = 100 + static_cast<int>(i); });
        pushed = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK_FALSE(pushed.load());

    auto first = drainAll(q, 4);
    producer.join();
    CHECK(pushed.load());
    CHECK(first == std::vector<int> { 0, 1, 2, 3 });
    CHECK(drainAll(q, 2) == std::vector<int> { 100, 101 });
}

TEST_CASE("Common::Queue::SpscQueue serializes simultaneous multi-producer pushes without loss", "[spsc][concurrency]")
{
    constexpr int producers = 4;
    constexpr int perProducer = 20000;
    Common::Queue::SpscQueue<int, 256> q;

    std::vector<Common::Compat::jthread> threads;
    for (int p = 0; p < producers; ++p) {
        threads.emplace_back([&q, p] {
            for (int i = 0; i < perProducer; i += 4) {
                q.pushBatch(4, [&](int& s, std::size_t k) { s = p * perProducer + i + static_cast<int>(k); });
            }
        });
    }

    auto all = drainAll(q, producers * perProducer);
    threads.clear();

    std::set<int> unique(all.begin(), all.end());
    CHECK(unique.size() == static_cast<std::size_t>(producers * perProducer));

    // Per-producer ordering must be preserved
    std::vector<int> last(producers, -1);
    for (int v : all) {
        int p = v / perProducer;
        CHECK(v > last[p]);
        last[p] = v;
    }
}

TEST_CASE("Common::Queue::SpscQueue stop() wakes a blocked consumer", "[spsc][cancellation]")
{
    Common::Queue::SpscQueue<int, 8> q;
    Common::Compat::stop_source src;
    std::atomic<bool> returned { false };

    Common::Compat::jthread consumer([&] {
        while (!src.stop_requested()) {
            q.drainContiguous(8, [](Common::Compat::span<const int>) { }, src.get_token());
        }
        returned = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(src.request_stop());
    q.stop();
    consumer.join();
    CHECK(returned.load());
}

TEST_CASE("Common::Queue::SpscQueue stop() wakes a producer blocked on a full queue", "[spsc][cancellation]")
{
    Common::Queue::SpscQueue<int, 2> q;
    REQUIRE(q.pushBatch(2, [](int& s, std::size_t) { s = 0; }));

    Common::Compat::stop_source src;
    std::atomic<int> result { -1 };
    Common::Compat::jthread producer([&] {
        result = q.pushBatch(1, [](int& s, std::size_t) { s = 1; }, src.get_token()) ? 1 : 0;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(src.request_stop());
    q.stop();
    producer.join();
    CHECK(result.load() == 0);
    CHECK(q.size() == 2);
}

TEST_CASE("Common::Queue::SpscQueue pushInPlace exposes contiguous slots and publishes only filled count", "[spsc][inplace]")
{
    Common::Queue::SpscQueue<int, 8> q;

    std::size_t exposed = 0;
    auto n = q.pushInPlace(16, [&](Common::Compat::span<int> slots) {
        exposed = slots.size();
        slots[0] = 7;
        slots[1] = 8;
        return std::size_t { 2 };
    });
    CHECK(exposed == 8); // bounded by capacity
    CHECK(n == 2);
    CHECK(q.size() == 2);

    // Filler returning 0 publishes nothing
    CHECK(q.pushInPlace(4, [](Common::Compat::span<int>) { return std::size_t { 0 }; }) == 0);
    CHECK(q.size() == 2);

    CHECK(drainAll(q, 2) == std::vector<int> { 7, 8 });
}

TEST_CASE("Common::Queue::SpscQueue pushInPlace span is bounded by the physical ring end", "[spsc][inplace]")
{
    Common::Queue::SpscQueue<int, 8> q;
    REQUIRE(q.pushBatch(6, [](int& s, std::size_t) { s = 0; }));
    drainAll(q, 6); // tail index = 6

    std::size_t exposed = 0;
    q.pushInPlace(8, [&](Common::Compat::span<int> slots) {
        exposed = slots.size();
        for (auto& s : slots) {
            s = 1;
        }
        return slots.size();
    });
    CHECK(exposed == 2);
}

TEST_CASE("Common::Queue::SpscQueue tryPushInPlace returns nullopt when another producer holds the lock", "[spsc][inplace]")
{
    Common::Queue::SpscQueue<int, 8> q;
    std::atomic<bool> holding { false };
    std::atomic<bool> release { false };

    Common::Compat::jthread holder([&] {
        q.pushInPlace(1, [&](Common::Compat::span<int> slots) {
            holding = true;
            while (!release) {
                std::this_thread::yield();
            }
            slots[0] = 1;
            return std::size_t { 1 };
        });
    });

    while (!holding) {
        std::this_thread::yield();
    }
    auto res = q.tryPushInPlace(1, [](Common::Compat::span<int> slots) { slots[0] = 2; return std::size_t { 1 }; });
    CHECK_FALSE(res.has_value());

    release = true;
    holder.join();

    res = q.tryPushInPlace(1, [](Common::Compat::span<int> slots) { slots[0] = 2; return std::size_t { 1 }; });
    REQUIRE(res.has_value());
    CHECK(*res == 1);
    CHECK(drainAll(q, 2) == std::vector<int> { 1, 2 });
}

TEST_CASE("Common::Queue::SpscQueue zero-copy Core::Threading::MessageTask round-trip via prepareInline/commitInline", "[spsc][inplace]")
{
    Common::Queue::SpscQueue<Core::Threading::MessageTask, 4> q;
    const std::string payload = "zero-copy payload";

    q.pushInPlace(1, [&](Common::Compat::span<Core::Threading::MessageTask> slots) {
        auto buf = slots[0].prepareInline("chan");
        std::memcpy(buf.data(), payload.data(), payload.size());
        slots[0].commitInline(payload.size());
        return std::size_t { 1 };
    });

    std::string gotPayload;
    std::string gotName;
    q.drainContiguous(4, [&](Common::Compat::span<const Core::Threading::MessageTask> tasks) {
        auto p = tasks[0].payload();
        gotPayload.assign(reinterpret_cast<const char*>(p.data()), p.size());
        gotName = tasks[0].channelName();
    });
    CHECK(gotName == "chan");
    CHECK(gotPayload == payload);
}
