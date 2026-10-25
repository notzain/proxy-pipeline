# Consolidated Live Telemetry Dashboard: Implementation Plan

## Goal

Combine the production telemetry dashboard and the topology-first mockup into one read-only operator experience. The topology overview should answer whether the chain is healthy and where pressure is building; the existing namespace diagnostics and raw metric table remain available for investigation.

This document records the agreed first-release scope and implementation approach. The standalone topology mockup remains unchanged.

## Current state

- `scripts/telemetry_dashboard.html` is the existing production metrics UI. It polls `/api/metrics` once per second and renders namespace tabs, summary metrics, latency distributions, and a raw metric table.
- `scripts/read_telemetry.py` receives StatsD datagrams, retains counters and gauges, and serves the metrics page and API. It has no chain topology input or endpoint.
- The core exporter in `core/src/telemetry/StatsDExporter.cpp` sends registered metrics periodically; `core/src/handler/IHandler.cpp` configures the exporter per handler.
- `scripts/proxy_runner/config.py` gives handlers a metric prefix containing the configured handler name. This provides some node identity in metric names, but not enough to infer the edges of a chain.
- `scripts/dashboard/live-topology-mockup.html` is a standalone visual prototype. Its chain, status, rates, and signals are synthetic and must not be presented as live data.
- The simulation dashboard and `SimBackend` emit per-message events and payload summaries for stepping and animation. That event path is not appropriate as the production telemetry feed.

## Product recommendation

Make the consolidated page a single dashboard with two views:

1. **Overview** — topology, chain-level KPIs, aggregated edge rates, node health/pressure summaries, and recent alerts.
2. **Metrics** — preserve the existing namespace tabs, latency distribution cards, and raw metric table.

The topology should be driven by declared chain configuration and aggregated metrics. Use only a small, capped number of decorative flow pulses per edge; their visual density must not scale with message rate. Display actual rates and byte rates as numbers. Node selection should reveal a compact detail panel with rates, latency, resource/queue health where available, and a path to existing metric diagnostics. Per-message inspection is out of scope for the initial feature.

Show explicit empty, stale, partial, and disconnected states. If no topology configuration is supplied, keep Metrics usable and show that topology is unavailable; do not substitute the illustrative mock chain.

## Proposed implementation work

### 1. Publish topology from the chain runner

**Likely files:** `scripts/chain_runner.py`, `scripts/read_telemetry.py`, and optionally a small helper under `scripts/proxy_runner/`.

- The chain runner will publish the chain it already loaded to the collector using an optional telemetry collector URL (CLI override and/or chain configuration). Do not require a second copy of the topology file at the collector.
- Publish a sanitized projection: chain/environment identifier, handler names and types, input/output channel names, and derived edges. Do not send arbitrary handler config, secrets, or payload-related settings.
- Support simple channels, multiple inputs/outputs, merge/split relationships, and route targets where those targets are statically declared. Document that dynamic destinations cannot be drawn unless explicitly modeled.
- Accept topology publication at a bounded, validated `POST /api/topology`; serve the latest sanitized document via `GET /api/topology`. Keep `/api/metrics` available with its current data fields for compatibility.
- When topology is absent or invalid, report that explicitly rather than fabricating nodes or silently returning an empty success-shaped topology.

### 2. Make telemetry aggregation reliable for the overview

**Likely files:** `scripts/read_telemetry.py`, `scripts/telemetry_dashboard.html`, and focused Python tests.

- Fix bandwidth rate calculation to use the byte counter delta, not the existing hard-coded 82 bytes/message.
- Replace heuristic global aggregation (for example, taking the maximum of handler counters) with documented chain-level rules based on topology. Distinguish ingress totals from per-stage processed totals so branching does not inflate ingress.
- Define metric semantics and units in the UI/API: counters are cumulative per handler prefix; rates derive from counter deltas; histogram values are cumulative buckets and must not be mislabeled as per-message samples.
- Track last-seen/receive time and counter resets or process restarts so the UI can show stale data and avoid negative/spurious rates.
- Bound storage for any directly exported timing samples; avoid retaining an ever-growing list for the lifetime of the collector.
- Add validation/tests for StatsD packet parsing, counter updates, rate/reset calculations, byte throughput, namespace grouping, and topology projection.

### 3. Characterize current production instrumentation

**Likely files:** `core/src/threading/*Strategy.cpp`, `core/src/handler/IHandler.cpp`, telemetry classes, `test/TestTelemetry.cpp`, and benchmark documentation.

- Audit/fix the existing `message_processing_duration_us` histogram before relying on it for displayed p50/p99. The receive strategies and base handler path both appear to observe timestamp-to-current-time; confirm whether this duplicates observations and whether the metric is message age or actual handler processing duration.
- First release uses currently available metrics only; missing queue/backpressure/readiness/outcome signals must appear as unavailable, not inferred from unrelated data.
- Do not add per-message network exports, JSON events, or dashboard callbacks.
- Measure telemetry-enabled versus disabled behavior at representative message sizes and high throughput; report throughput, CPU, tail latency, exporter traffic, and drops. The overhead target is intentionally unset until measurements are reviewed.

### 4. Consolidate the frontend

**Likely files:** `scripts/telemetry_dashboard.html`, and optionally extracted local CSS/JavaScript under `scripts/dashboard/`.

- Promote the topology-first layout from the prototype into the existing telemetry dashboard entry point.
- Retain namespace filtering, latency distributions, and raw metrics in a Metrics/Diagnostics view rather than deleting those capabilities.
- Consume `/api/topology` and `/api/metrics` separately. Render only configured topology nodes and use aggregated metrics to populate them.
- Implement static topology layout for branches/merges and clickable node/channel details. Keep rendering bounded: rates update at a human-useful cadence and SVG/canvas pulses are capped independently of actual throughput.
- Mark the view as live only when recent metric data is arriving; show metric age, missing signals, and collector disconnection clearly.
- Remove the external Tailwind runtime dependency when consolidating the page; use local styles so the dashboard does not depend on a third-party script at runtime.
- Leave `scripts/dashboard/live-topology-mockup.html` untouched as the original visual prototype unless a later request asks to retire it.

### 5. Secure and operationalize serving

**Likely files:** `scripts/read_telemetry.py` and deployment/run documentation.

- Default web serving to localhost; allow an explicit bind override for deployments. Remove wildcard CORS for the same-origin dashboard. Production remote access is expected to use an authenticated/restricted reverse proxy; this feature will not implement application authentication.
- Document supported topology config, collector configuration, freshness behavior, expected metric prefixes, and deployment/network requirements.
- Avoid exposing handler configuration or message content through topology/metrics APIs.

## Suggested sequence

1. Agree on the topology source and what the dashboard promises when topology or a metric is unavailable.
2. Correct/characterize the current metrics and add collector/parser/topology tests.
3. Add the topology endpoint and data projection.
4. Consolidate the frontend around the Overview and Metrics views.
5. Add missing bounded instrumentation and verify its overhead with benchmarks.
6. Secure and document the production deployment path.

## Acceptance criteria

- A configured chain produces a topology whose nodes and edges match its declared handlers and channels; unsupported dynamic routes are identified rather than guessed.
- The existing namespace details and raw metric diagnostics remain reachable from the consolidated page.
- Shown message/byte rates use counter deltas with correct units, and topology aggregations do not double-count downstream stages or branches as ingress.
- Missing, stale, reset, and disconnected telemetry states are visible and do not appear healthy.
- Flow animation has a fixed upper bound on simultaneous rendered pulses regardless of throughput.
- No message payload or per-message event stream is sent to the production dashboard.
- Focused tests cover collector parsing, rate math, topology projection, and aggregation rules; a documented benchmark compares telemetry-on and telemetry-off behavior.
- Production serving has a defined network/access-control boundary and does not expose raw handler secrets.

## Agreed first-release decisions

1. **Topology source:** The chain runner publishes its loaded topology to the collector; do not maintain a duplicate topology file.
2. **Instrumentation:** Use existing metrics for this release. Do not add new C++ hot-path metrics before assessing what the current data can reliably show.
3. **Serving security:** Bind to localhost by default and assume remote access is provided by an authenticated/restricted reverse proxy. No built-in authentication in the collector.
4. **Diagnostics:** Consolidate topology overview and existing metrics diagnostics as views in the same dashboard.
5. **Performance target:** Measure and report telemetry overhead first; set an acceptable budget after reviewing representative results.

## Remaining implementation caveats

- Existing StatsD metric names do not reliably identify separate processes that share the same handler prefix. Since the collector stores the latest absolute counter snapshot by metric name, concurrent processes with a shared prefix can overwrite each other's values. Ensure only one process per prefix publishes to a collector; do not claim fleet-wide aggregation unless instance identity is available.
- Current generic metrics do not provide queue depth, backpressure, readiness, or handler outcome counts. The first topology may show rates/latency where the existing prefix mapping is reliable, and clearly mark unsupported health signals as unavailable.
- The collector may host one active published topology at a time in this initial implementation. Supporting concurrent chains/environments on one collector requires an explicit chain identity and routing contract.

## Scope exclusions

- Replacing the deterministic simulation and trace dashboard.
- Replaying or animating every production message.
- Capturing/rendering payloads by default.
- Building a general-purpose metrics backend or alerting system.

## First implementation status

- `chain_runner.py` accepts `--telemetry-url`; alternatively set `config.telemetry.collector_url`. It posts the sanitized active chain topology to the collector.
- While a non-simulation chain is running, the runner republishes topology every five seconds, restoring it after collector restarts without restarting handlers.
- The chain runner launches each handler in its own process group and stops those groups on Ctrl+C, SIGTERM, or terminal hangup (SIGHUP); it removes its automatically created `/tmp/proxy_chain_*` runtime directory after shutdown. A caller-supplied `--work-dir` is retained. No user-space cleanup can run after SIGKILL or machine failure.
- The simulation coordinator and simulation dashboard are only started for the `sim` backend (or explicit headless simulation); production backends do not start the simulation UI.
- `read_telemetry.py` serves `GET /api/topology` and accepts bounded, validated `POST /api/topology`. It serves the dashboard stylesheet locally, keeps timing samples bounded, and defaults the web listener to `127.0.0.1`. The StatsD UDP listener remains separately configurable and defaults to `0.0.0.0`.
- `telemetry_dashboard.html` now combines an Overview topology view with the existing Metrics & Diagnostics view. Rates use counter deltas, the bandwidth view uses byte deltas, and stale/missing per-node data is marked unavailable. The Metrics & Diagnostics "All" summary uses topology source nodes for chain ingress and payload totals rather than presenting the largest downstream handler counter as chain ingress. The collector computes peak source ingress throughput from cumulative counter snapshots and exposes it for all browser clients. This is an interval-derived estimate, resets when the active chain identity changes, and is retained until the collector restarts.
- Run the collector with `python3 scripts/read_telemetry.py --host 0.0.0.0 --port 8125 --web-host 127.0.0.1 --web-port 8126`, then start the chain with `python3 scripts/chain_runner.py chains/<chain>.toml --backend <backend> --telemetry-url http://127.0.0.1:8126`. Configure each handler's `telemetry.statsd.host` and `telemetry.statsd.port` to reach the collector.
- The topology endpoint stores one active chain per collector. Keep the web listener on localhost unless it is placed behind the deployment's authenticated/restricted reverse proxy.
- The existing latency histogram measures message age from envelope timestamp at handler ingress, not handler execution time. The Overview labels it accordingly. Queue/readiness/CPU/application-result metrics and replica identity remain unavailable.
- Telemetry-on versus telemetry-off performance has **not yet been benchmarked**. A representative throughput, CPU, tail-latency, and exporter-traffic comparison is still required before production rollout; no overhead budget is claimed by this implementation.
