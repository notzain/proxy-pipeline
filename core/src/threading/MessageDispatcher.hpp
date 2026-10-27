#pragma once

#include "core/handler/IHandler.hpp"
#include <cstddef>
#include <mutex>
#include <span>
#include <string_view>

namespace Core::Threading::Detail {

void dispatchReceivedBatch(
    Handler::IHandler& handler,
    std::string_view channelName,
    std::span<const std::span<const std::byte>> rawMessages,
    std::size_t& invalidEnvelopeCount,
    std::mutex* handlerMutex);

} // namespace Core::Threading::Detail
