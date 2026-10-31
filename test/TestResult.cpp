#include "common/types/Result.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>

using Common::Types::Err;
using Common::Types::Ok;
using Common::Types::Result;

TEST_CASE("Result - Construct and basic state inspection", "[common][types][result]")
{
    Result<int, std::string> success = Ok(42);
    REQUIRE(success.isOk());
    REQUIRE_FALSE(success.isErr());
    REQUIRE(static_cast<bool>(success));
    CHECK(success.unwrap() == 42);

    Result<int, std::string> failure = Err(std::string("failed"));
    REQUIRE(failure.isErr());
    REQUIRE_FALSE(failure.isOk());
    REQUIRE_FALSE(static_cast<bool>(failure));
    CHECK(failure.unwrapErr() == "failed");
}

TEST_CASE("Result - unwrap throws on error and unwrapErr throws on ok", "[common][types][result]")
{
    Result<int, std::string> success = Ok(10);
    Result<int, std::string> failure = Err(std::string("error message"));

    CHECK_NOTHROW(success.unwrap());
    CHECK_THROWS_AS(success.unwrapErr(), std::logic_error);

    CHECK_NOTHROW(failure.unwrapErr());
    CHECK_THROWS_AS(failure.unwrap(), std::logic_error);
}

TEST_CASE("Result - unwrapOr fallback handling", "[common][types][result]")
{
    Result<int, std::string> success = Ok(100);
    Result<int, std::string> failure = Err(std::string("error"));

    CHECK(success.unwrapOr(5) == 100);
    CHECK(failure.unwrapOr(5) == 5);
}

TEST_CASE("Result - Pattern matching with match()", "[common][types][result]")
{
    Result<int, std::string> okRes = Ok(123);
    Result<int, std::string> errRes = Err(std::string("not found"));

    auto okResult = okRes.match(
        [](int v) { return v * 2; },
        [](const std::string&) { return -1; });
    CHECK(okResult == 246);

    auto errResult = errRes.match(
        [](int v) { return v * 2; },
        [](const std::string& err) { return static_cast<int>(err.size()); });
    CHECK(errResult == 9);
}

TEST_CASE("Result - Same type for T and E without ambiguity", "[common][types][result]")
{
    Result<int, int> success = Ok(10);
    Result<int, int> failure = Err(404);

    REQUIRE(success.isOk());
    CHECK(success.unwrap() == 10);

    REQUIRE(failure.isErr());
    CHECK(failure.unwrapErr() == 404);

    auto matchedOk = success.match(
        [](int v) { return v + 1; });
    CHECK(matchedOk == 11);
}

TEST_CASE("Result - Monadic map, mapErr, and andThen", "[common][types][result]")
{
    Result<int, std::string> res = Ok(21);
    auto mapped = res.map([](int x) { return x * 2; });
    REQUIRE(mapped.isOk());
    CHECK(mapped.unwrap() == 42);

    auto chained = mapped.andThen([](int x) -> Result<std::string, std::string> {
        return Ok(std::to_string(x));
    });
    REQUIRE(chained.isOk());
    CHECK(chained.unwrap() == "42");

    Result<int, std::string> errRes = Err(std::string("error"));
    auto mappedErr = errRes.mapErr([](const std::string& e) { return e + " handled"; });
    REQUIRE(mappedErr.isErr());
    CHECK(mappedErr.unwrapErr() == "error handled");

    auto bypassed = errRes.andThen([](int x) -> Result<std::string, std::string> {
        return Ok(std::to_string(x));
    });
    REQUIRE(bypassed.isErr());
    CHECK(bypassed.unwrapErr() == "error");
}

