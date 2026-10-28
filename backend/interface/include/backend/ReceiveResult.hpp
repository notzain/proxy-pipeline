#pragma once

#include "backend/Result.hpp"
#include "common/types/Variant.hpp"
#include <cstddef>
#include <string>
#include <system_error>
#include <vector>

namespace Backend {

struct ReceiveTimeout { };
struct ReceiveClosed { };

struct ReceiveFailure {
    std::error_code code;
};

using ReceiveError = Common::Types::Variant<ReceiveTimeout, ReceiveClosed, ReceiveFailure>;

struct ReceiveBatchFailure {
    std::size_t receivedCount;
    ReceiveError cause;
};

using BytesResult = Result<std::size_t, ReceiveError>;
using BatchResult = Result<std::size_t, ReceiveBatchFailure>;
using ReceiveMessageResult = Result<std::vector<std::byte>, ReceiveError>;

[[nodiscard]] inline ReceiveError receiveTimeout()
{
    return ReceiveTimeout { };
}

[[nodiscard]] inline ReceiveError receiveClosed()
{
    return ReceiveClosed { };
}

[[nodiscard]] inline ReceiveError receiveFailure(std::error_code code)
{
    return ReceiveFailure { code };
}

[[nodiscard]] inline bool isReceiveTimeout(const ReceiveError& error)
{
    return error.is<ReceiveTimeout>();
}

[[nodiscard]] inline bool isReceiveClosed(const ReceiveError& error)
{
    return error.is<ReceiveClosed>();
}

[[nodiscard]] inline std::string receiveErrorMessage(const ReceiveError& error)
{
    return error.match(
        [](const ReceiveClosed&) -> std::string { return "channel closed"; },
        [](const ReceiveFailure& failure) -> std::string { return failure.code.message(); },
        [](const ReceiveTimeout&) -> std::string { return "receive timed out"; });
}

} // namespace Backend
