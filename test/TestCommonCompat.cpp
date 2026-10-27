#include <catch2/catch_test_macros.hpp>
#include <common/compat/BitCast.hpp>
#include <common/compat/Jthread.hpp>
#include <common/compat/Latch.hpp>
#include <common/compat/Span.hpp>
#include <common/compat/StopToken.hpp>
#include <common/compat/StringView.hpp>
#include <common/compat/TypeTraits.hpp>
#include <numeric>
#include <vector>

TEST_CASE("Common Compat: Span basic operations and subspan", "[common][compat][span]")
{
    std::vector<int> data = { 1, 2, 3, 4, 5 };
    Common::Compat::span<int> s(data.data(), data.size());

    REQUIRE(s.size() == 5);
    REQUIRE(!s.empty());
    REQUIRE(s.front() == 1);
    REQUIRE(s.back() == 5);
    REQUIRE(s[2] == 3);

    auto sub = s.subspan(1, 3);
    REQUIRE(sub.size() == 3);
    REQUIRE(sub[0] == 2);
    REQUIRE(sub[2] == 4);

    auto bytes = Common::Compat::as_bytes(s);
    REQUIRE(bytes.size() == data.size() * sizeof(int));
}

TEST_CASE("Common Compat: bit_cast conversion", "[common][compat][bit_cast]")
{
    float orig = 1.0f;
    auto asInt = Common::Compat::bit_cast<std::uint32_t>(orig);
    auto backToFloat = Common::Compat::bit_cast<float>(asInt);
    REQUIRE(backToFloat == orig);
}

TEST_CASE("Common Compat: StringView starts_with and ends_with", "[common][compat][string_view]")
{
    std::string_view sv = "hello world";
    REQUIRE(Common::Compat::starts_with(sv, "hello"));
    REQUIRE_FALSE(Common::Compat::starts_with(sv, "world"));
    REQUIRE(Common::Compat::ends_with(sv, "world"));
    REQUIRE_FALSE(Common::Compat::ends_with(sv, "hello"));
    REQUIRE(Common::Compat::starts_with(sv, 'h'));
    REQUIRE(Common::Compat::ends_with(sv, 'd'));
}

TEST_CASE("Common Compat: Latch synchronization", "[common][compat][latch]")
{
    constexpr int numThreads = 4;
    Common::Compat::latch syncPoint(numThreads);
    std::atomic<int> counter { 0 };
    std::vector<Common::Compat::jthread> threads;

    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&syncPoint, &counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
            syncPoint.arrive_and_wait();
        });
    }

    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    REQUIRE(counter.load() == numThreads);
    REQUIRE(syncPoint.try_wait());
}

TEST_CASE("Common Compat: Jthread cooperative cancellation via stop_token", "[common][compat][jthread]")
{
    std::atomic<bool> threadSawStop { false };
    {
        Common::Compat::jthread worker([&threadSawStop](Common::Compat::stop_token st) {
            while (!st.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            threadSawStop.store(true);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        worker.request_stop();
    } // worker joins here on destruction

    REQUIRE(threadSawStop.load());
}

TEST_CASE("Common Compat: TypeTraits remove_cvref", "[common][compat][type_traits]")
{
    STATIC_REQUIRE(std::is_same_v<Common::Compat::remove_cvref_t<const int&>, int>);
    STATIC_REQUIRE(std::is_same_v<Common::Compat::remove_cvref_t<volatile int&&>, int>);
    STATIC_REQUIRE(std::is_same_v<Common::Compat::remove_cvref_t<const volatile int>, int>);
}
