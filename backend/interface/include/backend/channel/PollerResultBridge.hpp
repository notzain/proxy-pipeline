#pragma once

#include "backend/channel/ReceiveResult.hpp"
#include "common/net/PollerResult.hpp"

namespace Backend {

[[nodiscard]] inline ::Backend::ReceiveError fromPollerError(const Common::Net::PollerError& err)
{
    return err.match(
        [](const Common::Net::PollerClosed&) -> ::Backend::ReceiveError {
            return ::Backend::receiveClosed();
        },
        [](const Common::Net::PollerTimeout&) -> ::Backend::ReceiveError {
            return ::Backend::receiveTimeout();
        },
        [](const Common::Net::PollerFailure& f) -> ::Backend::ReceiveError {
            return ::Backend::receiveFailure(f.code);
        });
}

[[nodiscard]] inline ::Backend::BytesResult fromPollerBytesResult(const Common::Net::PollerBytesResult& res)
{
    if (res.hasValue()) {
        return ::Backend::BytesResult::success(res.value());
    }
    return ::Backend::BytesResult::failure(fromPollerError(res.error()));
}

[[nodiscard]] inline ::Backend::BatchResult fromPollerBatchResult(const Common::Net::PollerBatchResult& res)
{
    if (res.hasValue()) {
        return ::Backend::BatchResult::success(res.value());
    }
    return ::Backend::BatchResult::failure({ res.error().receivedCount,
        fromPollerError(res.error().cause) });
}

} // namespace Backend
