# Consolidated Live Telemetry Dashboard: Implementation Plan

## Goal

Combine the production telemetry dashboard and the topology-first mockup into one read-only operator experience. The topology overview should answer whether the chain is healthy and where pressure is building; the existing namespace diagnostics and raw metric table remain available for investigation.

This document records the agreed first-release scope and implementation approach. The standalone topology mockup remains unchanged.

## Current state

- `scripts/telemetry_dashboard.html` is the consolidated production dashboard. It polls `/api/metrics` and `/api/topology`, and provides topology-first Overview and Metrics & Diagnostics views.
- `scripts/read_telemetry.py` receives StatsD datagrams, retains counters and gauges, and serves the dashboard and APIs. The chain runner publishes sanitized topology to the collector.
- The core exporter in `core/src/telemetry/StatsDExporter.cpp` sends registered metrics periodically; `core/src/handler/IHandler.cpp` configures the exporter per handler.
- `scripts/proxy_runner/config.py` gives handlers a metric prefix containing the configured handler name. The topology projection connects those prefixes to declared handlers and channels.
- `scripts/dashboard/live-topology-mockup.html` is a standalone visual prototype. Its chain, status, rates, and signals are synthetic and must not be presented as live data.
- The simulation dashboard and `SimBackend` emit per-message events and payload summaries for stepping and animation. That event path is not appropriate as the production telemetry feed.

## Product recommendation

Make the consolidated page a single dashboard with two views:

1. **Overview** — topology, chain-level KPIs, aggregated edge rates, node health/pressure summaries, and recent alerts.
2. **Metrics** — preserve the existing namespace tabs, latency distribution cards, and raw metric table.

The topology should be driven by declared chain configuration and aggregated metrics. Use only a small, capped number of decorative flow pulses per edge; their visual density must not scale with message rate. Display actual rates and byte rates as numbers. Node selection should reveal a compact detail panel with rates, latency, resource/queue health where available, and a path to existing metric diagnostics. Per-message inspection is out of scope for the initial feature.

Show explicit empty, stale, partial, and disconnected states. If no topology configuration is supplied, keep Metrics usable and show that topology is unavailable; do not substitute the illustrative mock chain.

## Design decisions and implementation

### 1. Publish topology from the chain runner

**Files:** `scripts/chain_runner.py`, `scripts/read_telemetry.py`, and `scripts/proxy_runner/topology.py`.

- The runner publishes its loaded chain using `--telemetry-url` or `config.telemetry.collector_url`; it republishes every five seconds to recover after collector restarts.
- The published projection includes handler names/types, metric prefixes, channel names, derived edges, and unresolved output channels. Arbitrary handler configuration, secrets, and payload settings are excluded. Static route targets are included; dynamic destinations cannot be inferred.
- The collector validates bounded `POST /api/topology` requests and serves the latest projection at `GET /api/topology`. `/api/metrics` retains its existing fields and adds `peakThroughput`.
- Missing topology is reported explicitly; the dashboard does not substitute the illustrative mock chain.

### 2. Make telemetry aggregation reliable for the overview

**Files:** `scripts/read_telemetry.py`, `scripts/telemetry_dashboard.html`, and `test/test_live_telemetry.py`.

- Message and byte throughput are calculated from their respective counter deltas. Chain ingress totals use topology source nodes rather than summing every processing stage.
- The collector estimates peak ingress throughput from successive source counter snapshots, exposes it to all clients, resets it when chain identity changes, and retains it until the collector restarts. The value is an interval-based estimate, not a persisted or exact instantaneous maximum.
- Metric freshness is tracked per metric; stale/missing signals are shown as unavailable. Counter resets do not produce negative rates. Timing sample storage is bounded.
- The dashboard labels the latency breakdown by neutral numeric ranges without assigning qualitative performance judgments. Existing bucket boundaries and colors are retained.
- Focused tests cover collector parsing, topology validation/projection, peak calculation/reset, API behavior, and chain runner behavior.

### 3. Characterize current production instrumentation

**Files:** `core/src/threading/*Strategy.cpp`, `core/src/handler/IHandler.cpp`, telemetry classes, and benchmark documentation.

- The displayed `message_processing_duration_us` metric is labeled as message age measured from the envelope timestamp at handler ingress; it is not handler execution time.
- This release uses currently available metrics only. Queue depth, backpressure, readiness, application outcomes, CPU, and replica identity remain unavailable and are not inferred from unrelated measurements.
- No per-message production event stream or payload export was added.
- Telemetry-enabled versus disabled behavior has not yet been benchmarked. Measure throughput, CPU, tail latency, exporter traffic, and drops before setting an overhead budget.

### 4. Consolidate the frontend

**Files:** `scripts/telemetry_dashboard.html` and `scripts/telemetry_dashboard.css`.

- The Overview contains source KPIs, a configured chain topology, and selected-node details. Metrics & Diagnostics retains namespace filtering, latency distributions, and the raw metrics table.
- The topology SVG adapts rank spacing to the available width and redraws when the viewport changes. At constrained widths it stays inside its frame and scrolls internally instead of widening the page.
- The global 30-second source ingress activity graph is a responsive line chart with a labeled message-rate y-axis. Namespace selection does not reset its history.
- The page uses local CSS rather than an external Tailwind runtime. The standalone mockup remains unchanged.
- Latency range labels are descriptive only; colors remain as originally designed and are not presented as SLA/severity classifications.

### 5. Secure and operationalize serving

**Files:** `scripts/read_telemetry.py` and this feature document.

- The HTTP dashboard bind defaults to localhost and has an explicit override; the StatsD UDP listener is separately configurable. Same-origin dashboard serving does not require wildcard CORS.
- Remote access should use an authenticated/restricted reverse proxy; the collector does not implement application authentication.
- The topology projection excludes arbitrary handler configuration and message content.

The collector's configured UDP and HTTP ports must be available. Starting another collector on ports that are already bound currently causes its listener/server threads to report `Address already in use`; stop the existing collector or choose unused ports. Graceful startup error reporting for this collision is still needed.

## Tasks / checklist

- [x] Publish sanitized chain topology and validate collector API input.
- [x] Consolidate topology overview and existing metrics diagnostics.
- [x] Correct source ingress and byte-rate aggregation and show stale/missing metrics.
- [x] Add responsive topology layout, throughput line chart, and labeled y-axis.
- [x] Track peak ingress throughput in the collector and expose it to all clients.
- [x] Handle chain runner SIGINT/SIGTERM/SIGHUP shutdown, skip exited handlers, tolerate process-group exit races, and clean up launched handler process groups on graceful exit.
- [x] Add focused collector and runner regression tests.
- [ ] Improve collector startup error reporting when configured UDP or HTTP ports are already occupied.
- [ ] Benchmark telemetry-enabled versus telemetry-disabled performance at representative message sizes and throughput; agree on an overhead budget.
- [ ] Add instance identity before claiming aggregation across concurrent replicas that share metric prefixes.

## Acceptance criteria

- A configured chain produces a topology whose nodes and edges match its declared handlers and channels; unsupported dynamic routes are identified rather than guessed.
- The existing namespace details and raw metric diagnostics remain reachable from the consolidated page.
- Shown message/byte rates use counter deltas with correct units, and topology aggregations do not double-count downstream stages or branches as ingress.
- Missing, stale, reset, and disconnected telemetry states are visible and do not appear healthy.
- Flow animation has a fixed upper bound on simultaneous rendered pulses regardless of throughput.
- No message payload or per-message event stream is sent to the production dashboard.
- Focused tests cover collector parsing, rate math, topology projection, peak tracking, and runner behavior. A documented telemetry-on/off benchmark remains outstanding.
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
