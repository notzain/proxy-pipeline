# Feature: Centralized Message Validation & Ingress Accounting

## Overview

The execution strategies previously duplicated envelope validation, malformed-message handling, ingress message/byte counters, and dispatch. They also observed message age before calling `IHandler::processBatch()`, which observed it again. Centralizing ingress dispatch avoids inconsistent behavior between execution models and removes duplicate latency samples.

## Design Decisions

- Shared dispatcher code validates raw envelopes, counts malformed drops, increments valid-message and byte counters, and dispatches the valid views as a batch.
- Sequential, dedicated-thread, and thread-pool strategies retain ownership of polling, scheduling, queueing, flow affinity, and handler-access locking.
- `IHandler::processBatch()` remains the sole message-age observation point. The metric is elapsed time from the envelope timestamp to handler dispatch, not callback execution duration.
- Direct calls to `onMessageReceived()` or `onBatchReceived()` remain callback-level operations and do not perform framework ingress accounting.

## Performance Considerations

The change removes one clock read per dispatched batch and one histogram observation per eligible message. Each histogram observation updates multiple atomics, so this should reduce telemetry overhead, particularly with parallel workers. The shared dispatcher builds fixed-size span and view arrays without per-message allocation or payload copying; the extra helper call and span setup are expected to be minor. No throughput benchmark has been run, so the impact is not quantified.

## Tasks / Checklist

- [x] Extract common raw-envelope validation, malformed accounting, ingress counters, and handler batch dispatch.
- [x] Remove duplicate strategy-side latency observations.
- [x] Preserve strategy-specific scheduling, queues, flow affinity, and optional handler serialization.
- [x] Test mixed valid and malformed input across all three execution models, including exact ingress counts and one latency sample per valid message.
