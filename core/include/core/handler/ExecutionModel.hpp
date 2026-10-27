#pragma once

#include <cstddef>
#include <variant>

namespace Backend {
class IConfig;
}

namespace Core::Handler {

/**
 * @brief Single-threaded round-robin execution model.
 */
struct SequentialModel { };

/**
 * @brief Dedicated thread per registered input channel execution model.
 *
 * Handler callbacks and step() may overlap unless serializeHandlerAccess is true.
 */
struct DedicatedThreadModel {
    bool serializeHandlerAccess { false }; ///< Serialize processBatch() and step() calls when true.
};

/**
 * @brief Worker thread pool execution model with dedicated input channel poller.
 *
 * Handler callbacks and step() may overlap unless serializeHandlerAccess is true.
 */
struct ThreadPoolModel {
    std::size_t poolSize { 0 }; ///< Worker thread count (0 for hardware concurrency).
    bool strictFifo { true }; ///< Enforces strict per-flow FIFO order across workers.
    bool serializeHandlerAccess { false }; ///< Serialize processBatch() and step() calls when true.
};

/**
 * @brief Discriminated union of supported execution models and their parameters.
 */
using ExecutionModel = std::variant<SequentialModel, DedicatedThreadModel, ThreadPoolModel>;

/**
 * @brief Parses an ExecutionModel directly from a configuration object.
 *
 * Reads:
 * - `execution.model` (or `execution_model`): "sequential", "thread_pool", or "dedicated" (default).
 * - `execution.pool_size` (or `execution.threads`): Worker thread count for ThreadPoolModel (default: 0).
 * - `execution.strict_fifo`: Strict per-flow FIFO order flag (default: true).
 * - `execution.serialize_handler_access`: Serialize handler callbacks and step() (default: false).
 *
 * @param config Configuration store implementing Backend::IConfig.
 * @return ExecutionModel The parsed model variant.
 */
ExecutionModel parseExecutionModel(const ::Backend::IConfig& config);

} // namespace Core::Handler
