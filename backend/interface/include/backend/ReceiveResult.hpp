#pragma once

#include "backend/Result.hpp"
#include <cstddef>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

namespace Backend {

struct ReceiveTimeout { };
struct ReceiveClosed { };

struct ReceiveFailure {
    std::error_code code;
};

using ReceiveError = std::variant<ReceiveTimeout, ReceiveClosed, ReceiveFailure>;

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
    return std::holds_alternative<ReceiveTimeout>(error);
}

[[nodiscard]] inline bool isReceiveClosed(const ReceiveError& error)
{
    return std::holds_alternative<ReceiveClosed>(error);
}

[[nodiscard]] inline std::string receiveErrorMessage(const ReceiveError& error)
{
    if (std::holds_alternative<ReceiveClosed>(error)) {
        return "channel closed";
    }
    if (const auto* failure = std::get_if<ReceiveFailure>(&error)) {
        return failure->code.message();
    }
    return "receive timed out";
}

} // namespace Backend
