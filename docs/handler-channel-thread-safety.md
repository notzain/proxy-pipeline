# Handler and Channel Thread Safety

## Overview

The dedicated-thread and thread-pool execution models run input polling and message
processing on multiple threads. Handlers and channel implementations need clear
concurrency requirements, while handlers with non-thread-safe state need a way to
request serialized access without changing the default parallel throughput.

## Design Decisions

- Sequential execution invokes message callbacks and `step()` serially.
- Dedicated-thread execution polls each input channel on one thread. Callbacks for
  different channels may run concurrently, and `step()` runs on a separate thread.
- Thread-pool execution polls each input channel on one thread. Worker callbacks
  may run concurrently, and `step()` runs on a separate thread. Strict FIFO only
  orders messages assigned to the same flow/channel; it does not serialize handler
  state.
- Parallel handlers must synchronize shared mutable state by default. Output
  channel implementations must support concurrent sends or their callers must
  synchronize access.
- Set `execution.serialize_handler_access = true` to make the dedicated-thread
  and thread-pool strategies serialize each `processBatch()` call with other
  callbacks and with `step()`. The default is `false`; that path does not acquire
  the serialization mutex and retains existing parallel callback behavior.
- The runtime polls each registered input channel from one thread. Direct calls
  from additional threads remain the caller's synchronization responsibility.

## Example

An atomic counter can safely be incremented from parallel callbacks without
enabling serialized access:

```cpp
std::atomic<std::size_t> received { 0 };

Core::Message::MessageResult onMessageReceived(
    std::string_view,
    const Core::Message::MessageView&) override
{
    received.fetch_add(1, std::memory_order_relaxed);
    return Core::Message::Consumed {};
}
```

For handlers that update related non-thread-safe state or require callbacks and
`step()` not to overlap, enable the option in configuration:

```toml
[execution]
model = "thread_pool"
pool_size = 4
serialize_handler_access = true
```

Serialization applies only to handler entry points called by that execution
strategy. It does not make direct calls from external threads safe.

## Tasks / Checklist

- [x] Define concurrency guarantees for each built-in execution model.
- [x] Add opt-in serialized handler access for dedicated-thread and thread-pool models.
- [x] Document input polling and shared output channel requirements.
- [x] Test serialized access while preserving parallel callback behavior by default.
