#pragma once

#include "common/types/Variant.hpp"
#include <string_view>

namespace Backend {

/**
 * @brief Message was forwarded to one or more downstream channels.
 */
struct Forwarded {
    std::string_view channel { };
};

/**
 * @brief Message was consumed by a terminal sink handler (e.g. PrintHandler, UdpEgressHandler).
 */
struct Consumed {
};

/**
 * @brief Message was rejected/dropped with an explicit reason and error code.
 */
struct Dropped {
    std::string_view reason;
    int errorCode { 0 };
};

/**
 * @brief Strongly-typed result of message processing in handlers.
 */
using MessageResult = Common::Types::Variant<Forwarded, Consumed, Dropped>;

} // namespace Backend
