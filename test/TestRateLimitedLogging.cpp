#include "common/diagnostics/RateLimitedLogging.hpp"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Common::Diagnostics samples repeated log events with a bounded cadence", "[diagnostics]")
{
    CHECK(Common::Diagnostics::shouldLogEventCount(1));
    CHECK(Common::Diagnostics::shouldLogEventCount(8));
    CHECK_FALSE(Common::Diagnostics::shouldLogEventCount(9));
    CHECK_FALSE(Common::Diagnostics::shouldLogEventCount(1023));
    CHECK(Common::Diagnostics::shouldLogEventCount(1024));
    CHECK(Common::Diagnostics::shouldLogEventCount(2048));
}
