#pragma once

#include "log/ILogSink.hpp"
#include <spdlog/spdlog.h>

namespace Proxy::Log::Detail {

inline spdlog::level::level_enum toSpdlogLevel(Proxy::Log::Level level) noexcept
{
    switch (level) {
    case Proxy::Log::Level::Trace:
        return spdlog::level::trace;
    case Proxy::Log::Level::Debug:
        return spdlog::level::debug;
    case Proxy::Log::Level::Info:
        return spdlog::level::info;
    case Proxy::Log::Level::Warn:
        return spdlog::level::warn;
    case Proxy::Log::Level::Error:
        return spdlog::level::err;
    case Proxy::Log::Level::Critical:
        return spdlog::level::critical;
    case Proxy::Log::Level::Off:
        return spdlog::level::off;
    }
    return spdlog::level::info;
}

inline Proxy::Log::Level fromSpdlogLevel(spdlog::level::level_enum level) noexcept
{
    switch (level) {
    case spdlog::level::trace:
        return Proxy::Log::Level::Trace;
    case spdlog::level::debug:
        return Proxy::Log::Level::Debug;
    case spdlog::level::info:
        return Proxy::Log::Level::Info;
    case spdlog::level::warn:
        return Proxy::Log::Level::Warn;
    case spdlog::level::err:
        return Proxy::Log::Level::Error;
    case spdlog::level::critical:
        return Proxy::Log::Level::Critical;
    case spdlog::level::off:
        return Proxy::Log::Level::Off;
    default:
        return Proxy::Log::Level::Info;
    }
}

} // namespace Proxy::Log::Detail
