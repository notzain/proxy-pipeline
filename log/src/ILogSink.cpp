#include "log/ILogSink.hpp"
#include "log/Structured.hpp"

namespace Proxy::Log {

void ILogSink::logStructured(const StructuredLogRecord& record)
{
    log(record.level, record.message);
}

} // namespace Proxy::Log
