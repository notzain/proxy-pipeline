#pragma once

#include "backend/MessageResult.hpp"

namespace Core::Message {

using Forwarded = ::Backend::Forwarded;
using Consumed = ::Backend::Consumed;
using Dropped = ::Backend::Dropped;
using MessageResult = ::Backend::MessageResult;

} // namespace Core::Message
