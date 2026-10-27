#pragma once

#include "common/compat/Span.hpp"
#include "core/handler/IHandler.hpp"
#include <cstddef>
#include <mutex>
#include <string_view>

namespace Core::Threading::Detail {

void dispatchReceivedBatch(
    Handler::IHandler& handler,
    std::string_view channelName,
    Common::Compat::span<const Common::Compat::span<const std::byte>> rawMessages,
    std::size_t& invalidEnvelopeCount,
    std::mutex* handlerMutex);

} // namespace Core::Threading::Detail
