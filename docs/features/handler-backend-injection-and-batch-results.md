# Handler Backend Injection and Batch Results

## Overview

Handlers need their transport backend for channel creation and for reporting the
outcome of message processing. Supplying the backend during `configure()` made it
possible to configure a handler without a backend and required nullable backend
checks at runtime. Batch callbacks also returned no outcomes, preventing telemetry
and simulation from accurately recording mixed results in optimized batches.

## Design Decisions

- Inject `Backend::IBackend&` into the handler constructor and retain it as a
  non-null reference for the handler lifetime.
- Keep `configure()` focused on configuration data and channel setup.
- Populate an output span `std::span<Backend::MessageResult>` in `onBatchReceived()`
  backed by `Core::Memory::SmallSpanBuffer<Backend::MessageResult, 64>` to eliminate dynamic heap
  allocations during batch dispatch while preserving order and reporting mixed outcomes.
- Have batch dispatch report each result to the backend with the corresponding
  raw message when `m_backend.tracksMessageResults()` is enabled (e.g. SimBackend),
  skipping loop and virtual dispatch overhead on production fast-path backends.
- Require batch overrides to fill out one result per message in the provided destination span.
- Transition `configure()` to a non-virtual template method that guarantees `configureTelemetry()`
  is automatically invoked on the base class, delegating handler-specific channel/resource setup
  to a protected virtual `onConfigure(const Configuration&)` hook (mimicking `onMessageReceived` and `onBatchReceived`).

## Tasks / Checklist

- [x] Inject backends through handler constructors and remove backend parameters
  from `configure()`.
- [x] Pass output destination span `std::span<MessageResult>` to `onBatchReceived` and populate per-message results.
- [x] Eliminate dynamic heap allocations via `Core::Memory::SmallSpanBuffer<MessageResult, 64>` in `IHandler::processBatch`.
- [x] Update handler construction sites, handler implementations, and test suites.
- [x] Separate telemetry setup into `IHandler::configureTelemetry()`.
- [x] Implement NVI `IHandler::configure()` delegating to `onConfigure()` and automatically running `configureTelemetry()`.
- [x] Update all 10 concrete handler classes and tests from `configure()` to `onConfigure()`, removing `IHandler::configure(config)` calls.
