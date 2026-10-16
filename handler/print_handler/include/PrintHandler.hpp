#pragma once

#include "backend/IBackend.hpp"
#include "core/IHandler.hpp"
#include <memory>

namespace Proxy::Handler {

/**
 * @brief Handler that implements Core::IHandler by delegating nextMessage to a backend
 * and printing any received messages to standard output.
 */
class PrintHandler : public Core::IHandler {
public:
    /**
     * @brief Constructs a PrintHandler with a given backend transport.
     *
     * @param backend Unique pointer to the underlying transport backend.
     */
    explicit PrintHandler(std::unique_ptr<::Backend::IBackend> backend);

    ~PrintHandler() override = default;

    /**
     * @brief Prints the received message buffer to standard output using fmt.
     *
     * @param buffer Non-owning view of the raw byte buffer received.
     */
    void onMessageReceived(std::span<const std::byte> buffer) override;

    /**
     * @brief Retrieves the next message by polling the configured backend.
     *
     * @param timeout Maximum duration to wait before returning.
     * @return std::optional<Buffer> Received message buffer or std::nullopt.
     */
    std::optional<Buffer> nextMessage(Timeout timeout) override;

private:
    std::unique_ptr<::Backend::IBackend> m_backend;
};

} // namespace Proxy::Handler
