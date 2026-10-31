#pragma once

#include "common/types/Result.hpp"
#include "common/types/Variant.hpp"
#include <cstddef>
#include <string>
#include <system_error>

namespace Common::Net {

struct ReceiveTimeout { };
struct ReceiveClosed { };

struct ReceiveFailure {
    std::error_code code;
};

class ReceiveError : public Common::Types::Variant<ReceiveTimeout, ReceiveClosed, ReceiveFailure> {
public:
    using Variant::Variant;

    [[nodiscard]] std::string message() const
    {
        return match(
            [](const ReceiveClosed&) -> std::string { return "channel closed"; },
            [](const ReceiveFailure& failure) -> std::string { return failure.code.message(); },
            [](const ReceiveTimeout&) -> std::string { return "receive timed out"; });
    }
};

struct ReceiveBatchFailure {
    std::size_t receivedCount;
    ReceiveError cause;
};

using BytesResult = Common::Types::Result<std::size_t, ReceiveError>;
using BatchResult = Common::Types::Result<std::size_t, ReceiveBatchFailure>;

} // namespace Common::Net
