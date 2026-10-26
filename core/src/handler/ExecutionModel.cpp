#include "core/handler/ExecutionModel.hpp"
#include "backend/IConfig.hpp"
#include <string>

namespace Core::Handler {

ExecutionModel parseExecutionModel(const ::Backend::IConfig& config)
{
    std::string modelName = config.getOr<std::string>("execution.model", config.getOr<std::string>("execution_model", "dedicated"));
    if (modelName == "sequential" || modelName == "single") {
        return SequentialModel { };
    }
    if (modelName == "thread_pool" || modelName == "pool") {
        std::size_t poolSize = static_cast<std::size_t>(config.getOr<int>("execution.pool_size", config.getOr<int>("execution.threads", 0)));
        bool strictFifo = config.getOr<bool>("execution.strict_fifo", true);
        return ThreadPoolModel { .poolSize = poolSize, .strictFifo = strictFifo };
    }

    return DedicatedThreadModel { };
}

} // namespace Core::Handler
