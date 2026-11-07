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

### Full redesign or replacement of the visual style — defer

- **Potential value:** A broader information-architecture and visual-hierarchy redesign could further reduce cognitive load across the four workspaces.
- **Evidence and cost:** The current gaps found in this pass were addressable with clearer workflow guidance, shared status/navigation, safe recovery, and targeted responsive/accessibility fixes. No user research or task-completion data currently demonstrates that the visual system itself is the remaining blocker. A redesign would be high-cost and risks replacing familiar workflows without evidence.
- **Recommendation:** Do not start a redesign now. First test the documented quickstart and common edit/run/inspect tasks with representative users; revisit only if recurring friction remains after these improvements.

### Changing chain execution semantics or backend defaults — defer behavior changes

- **Evidence:** Chains generally declare their backend explicitly, the builder serializer falls back to `file` when one is absent (`scripts/proxy_runner/config.py`), and the quickstart explicitly asks users to select `sim` for an interactive run (`README.md`). The chain examples cover `file`, `socket`, `shm`, and `sim`, each with different transport configuration.
- **Potential value and risk:** A better default or automatic recommendation could reduce setup friction for a specific workflow. A silent global change could instead select incompatible handler binaries or transport settings and change established chain behavior.
- **Recommendation:** Preserve current defaults and execution behavior. If evidence later shows backend selection is a repeated source of failure, prototype an explicit workflow preset or context-aware recommendation before considering any default change.

### Replacing the frontend framework or adding a dependency — not justified

- **Evidence:** The UI is served as static HTML/JavaScript through the existing studio runner, uses vendored Petite Vue, and has no frontend package manifest or build pipeline. The improvements in this plan were implemented without a new dependency.
- **Potential value and cost:** A component framework could help if shared state and repeated UI become difficult to maintain, but replacement alone would not resolve the observed workflow friction. It would add build, dependency, packaging, and offline-maintenance requirements.
- **Recommendation:** Do not replace the current stack for this UX effort. Revisit only when a concrete capability or maintainability problem cannot be solved cleanly within the existing approach.

**Decision summary:** Defer the full redesign and behavior-changing defaults; do not pursue a framework replacement without new evidence. Prefer measured, task-specific changes if future usability testing identifies remaining problems.

## Phase 2 implementation summary

The README now provides a first-pipeline path using existing presets and sample chains. The portal explains the workflow between workspaces, and builder, simulation, PCAP, and telemetry empty states recommend a next action.

## Phase 3 implementation summary

The builder tracks edits against the last loaded/saved definition, warns before leaving, keeps save failures in the open dialog, and provides a run retry in the error viewer. Existing files require explicit overwrite confirmation in the UI and API. Runner restart now waits for a successful stop/status check before starting again.

## Phase 4 implementation summary

All four workspaces now share the same runner status banner, visible keyboard focus, and accessible workspace-menu state (including Escape-to-close and focus return). Builder palette actions and simulation packet items use native buttons; trace entries and builder connections support keyboard selection. Icon-only actions have explicit accessible names. Mobile viewport metadata and the portal's narrow-screen grid/action layout were corrected; all workspaces fit a 375 px viewport without horizontal overflow.
