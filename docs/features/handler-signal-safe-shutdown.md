# Signal-Safe Handler Shutdown

## Overview

`HandlerRunner` previously called `stop()` from its POSIX signal callback. Strategy
shutdown may use atomics, queues, and thread operations that are not async-signal-safe,
and the global active-runner pointer silently limited signal ownership to one runner.

## Design Decisions

- The signal callback only stores to a lock-free atomic notification flag.
- A normal `std::jthread` coordinator observes the flag and invokes the strategy's
  `stop()` and `wait()` outside signal context. The same coordinator owns both operations,
  preventing them from racing each other.
- `HandlerRunner::stop()` requests shutdown; the coordinator applies it within its
  10 ms polling interval. `HandlerRunner::wait()` joins the coordinator before
  unregistering signal handlers.
- The runner saves and restores the pre-existing SIGINT and SIGTERM dispositions.
- Signal ownership is process-wide and exclusive. Starting signal handling on a second
  runner while another runner owns it throws `std::logic_error`; it does not replace the
  active runner's signal handlers.
- The coordinator polls the notification every 10 ms, keeping the signal callback minimal
  without relying on condition variables, logging, allocation, or strategy calls from it.

## Tasks / Checklist

- [x] Limit the POSIX callback to an async-signal-safe notification.
- [x] Perform strategy shutdown from normal thread context.
- [x] Make single-runner signal ownership explicit and restore prior dispositions.
- [x] Test SIGINT/SIGTERM shutdown and conflicting runner ownership.
