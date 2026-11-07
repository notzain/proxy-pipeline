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

- [x] Add a concise “first pipeline” quickstart from prerequisites through a successful sample run.
- [x] Provide guided empty states with a recommended next action for chains, PCAPs, simulation, and telemetry.
- [x] Explain the relationship between the portal, builder, simulator, and telemetry dashboard in-product.

### Phase 3 — Make editing and operation safer

- [x] Add an unsaved-changes indicator and warn before navigating away from builder edits.
- [x] Keep save and run context visible when an operation fails, with actionable retry guidance.
- [x] Require clear confirmation before overwriting an existing chain and report the resulting file location.
- [x] Ensure restart only starts again after a successful stop.

### Phase 4 — Unify interaction and accessibility

- [x] Establish shared navigation and status language across all workspaces.
- [x] Audit keyboard access, focus order, accessible names, live announcements, and small-screen layouts.
- [x] Replace clickable non-semantic controls with buttons and links where appropriate.

## Phase 1 implementation summary

Phase 1 made the portal's chain selection actionable and surfaces request state where users need it. A selected chain can be opened in the builder, saved back to the same repository-relative path, run in simulation, and carried through workspace links. Each workspace shows a shared runner status summary. The server constrains chain reads and writes to `chains/`.

## Acceptance criteria

- The portal reports failed chain/PCAP loads and status polling in the page, with a way to retry.
- Empty chain and PCAP lists explain that no choices are available instead of showing a blank selector.
- Runner start/stop failures are visible in-page; browser alert dialogs are not required to understand an error.
- “Edit Selected Chain” opens the builder with that selected chain loaded.
- Existing builder behavior without a `chain` query parameter remains unchanged.
- The chain-loading API rejects paths outside `chains/` and reports missing/invalid chain files.
- Focused frontend-runner tests pass.
- Workspace menus expose expanded state, close with Escape, and return keyboard focus to their trigger.
- Primary builder and simulator actions have keyboard support and accessible names.
- Portal, Builder, Simulator, and Telemetry render without horizontal overflow at a 375 px viewport.

## Out of scope for this slice

- A full redesign or replacement of the existing visual style.
- Changing chain execution semantics or backend defaults.
- Adding a new frontend framework or dependency.

## Deferred-scope investigation

The deferred items will be evaluated after the planned workflow phases. The first implementation pass uses the current framework and visual system; it does not yet justify a full redesign or behavior-changing backend defaults.

## Phase 2 implementation summary

The README now provides a first-pipeline path using existing presets and sample chains. The portal explains the workflow between workspaces, and builder, simulation, PCAP, and telemetry empty states recommend a next action.

## Phase 3 implementation summary

The builder tracks edits against the last loaded/saved definition, warns before leaving, keeps save failures in the open dialog, and provides a run retry in the error viewer. Existing files require explicit overwrite confirmation in the UI and API. Runner restart now waits for a successful stop/status check before starting again.

## Phase 4 implementation summary

All four workspaces now share the same runner status banner, visible keyboard focus, and accessible workspace-menu state (including Escape-to-close and focus return). Builder palette actions and simulation packet items use native buttons; trace entries and builder connections support keyboard selection. Icon-only actions have explicit accessible names. Mobile viewport metadata and the portal's narrow-screen grid/action layout were corrected; all workspaces fit a 375 px viewport without horizontal overflow.
