# Input Channel Collection Encapsulation

## Overview

`IHandler` previously exposed its input-channel vector, including the `unique_ptr` objects that owned each channel. This coupled the event-loop strategies and other callers to the handler's internal container and ownership representation.

The collection is now private and stores channel ownership directly, without a second per-channel name field. Callers can query its size and visit channels through a callback receiving a non-owning channel reference; dispatch uses `IInputChannel::name()`. Handlers likewise use bound output channels as the source of output names rather than storing duplicate name members.

## Design Decisions

- Keep ownership of input channels exclusively in `IHandler`.
- Provide `inputChannelCount()` for count-only queries and `forEachInputChannel()` for iteration.
- Pass channel references rather than pointers or owning wrappers to visitors.
- Remove the registration-name override because channel identity belongs to `IInputChannel`; backend-created channels already receive the logical channel name.
- Keep configured input-channel names local to handler setup; do not retain a second name or observer for channels already owned by `IHandler`.
- Use `IOutputChannel::name()` for forwarding results and logs after an output channel is bound; keep configured names local to creation and error reporting.
- Update all execution strategies and `HandlerRunner` to consume the new API without changing polling, dispatch, or scheduling behavior.

## Tasks / Checklist

- [x] Store input channels directly in the private owning vector.
- [x] Replace `getInputChannels()` with count and visitor APIs.
- [x] Refactor `HandlerRunner` and all threading strategies to use the encapsulated API.
- [x] Test channel count, visitation order, channel names, and channel identity.
- [x] Remove handler output-name members when the bound output channel provides the same name.
