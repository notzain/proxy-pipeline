#pragma once

#include "core/config/ConfigItem.hpp"
#include "core/config/ConfigSection.hpp"
#include <cstdint>
#include <string>

namespace Proxy::Handler {

/**
 * @brief Strongly-typed configuration section for UdpEgressHandler.
 */
struct UdpEgressConfig : public Core::Config::ConfigSection {
    Core::Config::ConfigItem<std::string> inputChannel { this, "egress.input_channel", "egress_in" };
    Core::Config::ConfigItem<std::string> mode { this, "egress.mode", "reply_to_origin" };
    Core::Config::ConfigItem<std::string> destHost { this, "egress.dest_host", "127.0.0.1" };
    Core::Config::ConfigItem<int> destPort { this, "egress.dest_port", 9200 };
};

} // namespace Proxy::Handler
