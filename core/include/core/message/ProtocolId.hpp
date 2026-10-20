#pragma once

#include <cstdint>

namespace Core::Message {

/**
 * @brief Identifies the network or application protocol carried in the envelope.
 */
enum class ProtocolId : std::uint8_t {
    Raw = 0, ///< Unadorned or generic application payload (default/testing)
    Udp = 1, ///< UDP datagram stream with UdpMetadata
    Tcp = 2, ///< TCP stream connection with TcpMetadata
    Mqtt = 3 ///< MQTT pub/sub message with MqttMetadata
};

} // namespace Core::Message
