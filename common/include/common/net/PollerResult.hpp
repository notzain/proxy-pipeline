#pragma once

#include "common/compat/Span.hpp"
#include "common/types/Result.hpp"
#include "common/types/Variant.hpp"
#include <cstddef>
#include <string>
#include <system_error>

namespace Common::Net {

struct PollerTimeout { };
struct PollerClosed { };

struct PollerFailure {
    std::error_code code;
};

using PollerError = Common::Types::Variant<PollerTimeout, PollerClosed, PollerFailure>;

struct PollerBatchFailure {
    std::size_t receivedCount;
    PollerError cause;
};

using PollerBytesResult = Common::Types::Result<std::size_t, PollerError>;
using PollerBatchResult = Common::Types::Result<std::size_t, PollerBatchFailure>;

[[nodiscard]] inline PollerError pollerTimeout() noexcept
{
    return PollerTimeout { };
}

[[nodiscard]] inline PollerError pollerClosed() noexcept
{
    return PollerClosed { };
}

[[nodiscard]] inline PollerError pollerFailure(std::error_code code) noexcept
{
    return PollerFailure { code };
}

[[nodiscard]] inline bool isPollerTimeout(const PollerError& error) noexcept
{
    return error.is<PollerTimeout>();
}

[[nodiscard]] inline bool isPollerClosed(const PollerError& error) noexcept
{
    return error.is<PollerClosed>();
}

[[nodiscard]] inline std::string pollerErrorMessage(const PollerError& error)
{
    return error.match(
        [](const PollerClosed&) -> std::string { return "channel closed"; },
        [](const PollerFailure& failure) -> std::string { return failure.code.message(); },
        [](const PollerTimeout&) -> std::string { return "receive timed out"; });
}

} // namespace Common::Net
