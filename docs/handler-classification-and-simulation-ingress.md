# Feature: Handler Classification & Simulation Ingress Abstraction

## Overview
Currently, the simulation mode (`scripts/proxy_runner/coordinator.py` and `scripts/dashboard/js/canvas.js`) assumes that any handler without an explicit input channel is an autonomous "generator" (e.g. `dummy_send` emitting periodic synthetic messages).

However, external network listeners such as `udp_ingress_handler` (and future handlers like `http_ingress_handler`) also have no input channels from the pipeline's internal transport perspective—they read directly from external UDP sockets or HTTP POST endpoints. As a result:
- Ingress handlers are erroneously flagged as generators unless hardcoded string checks (`type != "udp_ingress_handler"`) are scattered in Python scripts.
- The simulation coordinator fails to correctly handle mock packet injection (e.g. PCAP replay) for different protocols.
- The visual simulation dashboard cannot clearly distinguish autonomous generators from external network gateways.

This feature establishes an explicit, strongly typed classification system for handlers in C++, equips CMake with decoupled per-handler sidecar descriptors (`<handler>.json`), and introduces a pluggable protocol injection strategy for simulation.

---

## Design Decisions

1. **Rich Classification Model**:
   Handlers are categorized into distinct roles:
   - `Generator`: Autonomous synthetic message source driven by internal timers / `step()` (e.g. `dummy_send`).
   - `ExternalIngress`: Gateway receiving data from external protocols/networks outside the pipeline bus (e.g. `udp_ingress_handler`, HTTP receiver).
   - `ExternalEgress`: Gateway sending pipeline messages to outside external networks/sinks (e.g. `udp_egress_handler`).
   - `Processor`: Standard pipeline intermediate node (transforms, routes, filters, splits, merges) receiving from internal input channels and forwarding to output channels.

2. **Protocol Association**:
   External ingress and egress handlers declare their external protocol (`Udp`, `Http`, `None`, etc.) via a dedicated enum trait, enabling protocol-specific handling.

3. **C++ Native Traits**:
   `Core::Handler::IHandler` declares `virtual HandlerRole role() const` and `virtual HandlerProtocol protocol() const`.
   Concrete handler implementations override these methods to declare their true identity in C++.

4. **Decoupled Per-Handler Sidecar Descriptors (`<handler>.json`)**:
   - Rather than relying on a monolithic manifest or running binary CLI flags at runtime, each handler produces its own sidecar descriptor file alongside the binary: `<bin_dir>/<handler_name>.json`.
   - `proxy_add_handler_all_backends` in `cmake/ProxyBackends.cmake` accepts `ROLE` and `PROTOCOL` arguments (defaulting to `processor` and `none`).
   - During build/packaging, CMake outputs `${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${handler_base_name}.json`.
   - **Multi-repo decoupled**: Handlers can be split into separate Git repositories in the future; each repo's build automatically emits its own sidecar descriptor file without centralized coordination.
   - Python tools (`chain_runner.py`, `coordinator.py`) resolve metadata by inspecting `<bin_dir>/<handler_type>.json` directly.

5. **Simulation Injection via Pluggable Injectors**:
   - The simulation coordinator uses protocol-specific injectors (e.g. `UdpPacketInjector` replaying PCAP frames to the bound UDP socket, and future `HttpPayloadInjector` issuing mock POST requests).
   - `Generator` nodes continue to have their timers paused in simulation mode (`delay_ms = 999999999`) and are triggered manually on demand via `⚡ Trigger Emit`.

6. **Dashboard UI Distinction**:
   - `Generator` nodes: highlighted with a generator badge (`⚡ Generator`) and manual "Trigger Emit" button.
   - `ExternalIngress` nodes: styled with a network badge (`🌐 Ingress [UDP]`) showing injected packet counters and port bindings, without generator emit buttons.

---

## Tasks

### Phase 1: Core C++ Abstractions & Handler Annotations
- [x] Add `HandlerRole` and `HandlerProtocol` enums to `core/include/core/handler/HandlerTraits.hpp`.
- [x] Add `virtual HandlerRole role() const` and `virtual HandlerProtocol protocol() const` to `Core::Handler::IHandler`.
- [x] Update built-in handlers:
  - [x] `DummySendHandler`: `HandlerRole::Generator`, `HandlerProtocol::None`.
  - [x] `UdpIngressHandler`: `HandlerRole::ExternalIngress`, `HandlerProtocol::Udp`.
  - [x] `UdpEgressHandler`: `HandlerRole::ExternalEgress`, `HandlerProtocol::Udp`.
  - [x] Other handlers (`PrintHandler`, `SplitHandler`, `MergeHandler`, `RouteHandler`, `XmlVerificationHandler`): default to `HandlerRole::Processor`.

### Phase 2: Decoupled CMake Sidecar Descriptors
- [x] Update `proxy_add_handler_all_backends` in `cmake/ProxyBackends.cmake` to support `ROLE <role>` and `PROTOCOL <protocol>` arguments.
- [x] Configure `cmake/ProxyBackends.cmake` to generate `<bin_dir>/<handler_base_name>.json` containing:
  ```json
  {
    "name": "<handler_base_name>",
    "role": "<role>",
    "protocol": "<protocol>"
  }
  ```
- [x] Update handler `CMakeLists.txt` invocations to specify their respective `ROLE` and `PROTOCOL`.

### Phase 3: Python Tooling & Simulation Coordinator
- [x] Create `scripts/proxy_runner/handlers.py` with descriptor loading logic (`load_handler_descriptor(bin_dir, handler_type)`), falling back to defaults if absent.
- [x] Refactor `scripts/chain_runner.py` to inspect handler roles instead of string-matching `"udp_ingress_handler"`.
- [x] Implement pluggable packet injectors in `scripts/proxy_runner/injectors.py` and integrate into `coordinator.py`:
  - [x] Abstract `BasePacketInjector`.
  - [x] `UdpPacketInjector` for UDP ingress.
  - [x] `UnixSocketPacketInjector` for internal channels.
- [x] Remove hardcoded `is_generator` hacks based solely on input channel absence in `coordinator.py`.
- [x] Fix concurrent generator trigger queueing in `coordinator.py` to preserve multiple in-flight branches across different generators without overwriting active messages.

### Phase 4: Dashboard Web Visualizer
- [x] Pass handler role and protocol metadata in topology endpoint to `scripts/dashboard/js/canvas.js`.
- [x] Update node rendering in `scripts/dashboard/js/canvas.js` to visually differentiate `generator` vs `ingress` vs `processor`.
- [x] Restrict `⚡ Trigger` action solely to actual `Generator` nodes.
- [x] Add ingress protocol tags (`UDP`, `HTTP`) to node badges.

### Phase 5: Verification & Testing
- [x] Verify build generates the `.json` sidecars in `bin/`.
- [x] Run unit and simulation tests (`ctest` / `proxy_tests` - 121,328 assertions passed across 64 test cases).
- [x] Verify `example_shm_ingress_chain.toml` metadata inspection (`role: external_ingress`, `protocol: udp`).
- [x] Verify generator chain (`example_file_chain.toml` / `dummy_send` with `role: generator`).
