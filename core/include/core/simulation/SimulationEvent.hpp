#pragma once

#include "core/message/MessageEnvelope.hpp"
#include "core/message/MessageView.hpp"
#include "core/simulation/MockInputChannel.hpp"
#include "core/simulation/MockOutputChannel.hpp"
#include "core/simulation/PcapReader.hpp"
#include "core/simulation/SimulationBackend.hpp"
#include "core/simulation/SimulationClock.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Core::Simulation {

/**
 * @brief Structured simulation event emitted as messages traverse handlers and channels.
 */
struct SimulationEvent {
    std::uint64_t sequenceId { 0 };
    std::uint64_t timestampNs { 0 };
    std::string eventType;          // "ingress", "hop", "egress", "drop"
    std::string sourceHandler;
    std::string targetHandler;
    std::string channelName;
    std::string clientIp;
    std::uint16_t clientPort { 0 };
    std::string payloadSummary;
    std::string payloadFull;
    double transitLatencyUs { 0.0 };
    bool isSuccess { true };
    std::string detail;
};

using SimulationEventCallback = std::function<void(const SimulationEvent&)>;

} // namespace Core::Simulation

