# Usability & User Experience Improvement Plan

## Goal

Make Proxy feel like one understandable workflow rather than a collection of capable but disconnected tools. Prioritize discoverability, clear feedback, safe recovery, and continuity between designing, running, simulating, and monitoring a chain.

## Product principles

- **Task-first:** help users complete a concrete goal before exposing implementation detail.
- **Visible state:** show whether data is loading, unavailable, stale, empty, or ready.
- **Continuous workflow:** carry the selected chain and relevant run context between workspaces.
- **Safe by default:** make destructive or persistent actions clear and recoverable.
- **Progressive disclosure:** keep common actions obvious while leaving advanced controls available.
- **Accessible interaction:** provide semantic controls, keyboard access, focus visibility, and readable status updates.

## Phased plan

### Phase 1 — Connect the core workflow

- [x] Add an explicit portal-to-builder action that opens the selected chain.
- [x] Show portal loading, empty, runner-connection, and action feedback rather than relying on console messages or browser alerts.
- [x] Load a requested chain into the builder through a path-constrained API endpoint.
- [x] Preserve chain context through builder save/run and simulator/telemetry navigation.
- [x] Make the active chain, backend, and run state consistent across workspaces.

### Phase 2 — Improve first-use success

- [ ] Add a concise “first pipeline” quickstart from prerequisites through a successful sample run.
- [ ] Provide guided empty states with a recommended next action for chains, PCAPs, simulation, and telemetry.
- [ ] Explain the relationship between the portal, builder, simulator, and telemetry dashboard in-product.

### Phase 3 — Make editing and operation safer

- [ ] Add an unsaved-changes indicator and warn before navigating away from builder edits.
- [ ] Keep save and run context visible when an operation fails, with actionable retry guidance.
- [ ] Require clear confirmation before overwriting an existing chain and report the resulting file location.
- [ ] Ensure restart only starts again after a successful stop.

### Phase 4 — Unify interaction and accessibility

- [ ] Establish shared navigation and status language across all workspaces.
- [ ] Audit keyboard access, focus order, accessible names, live announcements, and small-screen layouts.
- [ ] Replace clickable non-semantic controls with buttons and links where appropriate.

## Current implementation slice

This phase makes the portal's chain selection actionable and surfaces request state where users need it. A selected chain can be opened in the builder, saved back to the same repository-relative path, run in simulation, and carried through workspace links. Each workspace shows a shared runner status summary. The server constrains chain reads and writes to `chains/`.

## Acceptance criteria

- The portal reports failed chain/PCAP loads and status polling in the page, with a way to retry.
- Empty chain and PCAP lists explain that no choices are available instead of showing a blank selector.
- Runner start/stop failures are visible in-page; browser alert dialogs are not required to understand an error.
- “Edit Selected Chain” opens the builder with that selected chain loaded.
- Existing builder behavior without a `chain` query parameter remains unchanged.
- The chain-loading API rejects paths outside `chains/` and reports missing/invalid chain files.
- Focused frontend-runner tests pass.

## Out of scope for this slice

- A full redesign or replacement of the existing visual style.
- Changing chain execution semantics or backend defaults.
- Adding a new frontend framework or dependency.
