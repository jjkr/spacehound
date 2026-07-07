# SpaceRabbit Supervisor/Daemon Migration Design

> Historical note: this document describes the earlier Rust/Tauri supervisor
> migration plan. The current macOS app is an AppKit menu bar app that links the
> C++ runtime in-process, so this document should be treated as background
> context rather than the active architecture.

## Summary

This document describes how `~/work/spacerabbit` should evolve from a Rust/Tauri app that owns nearly all runtime behavior into a split architecture:

- a C++ macOS daemon in `~/work/spacerabbit-core` owns the low-latency system integration work
- the Rust/Tauri app in `~/work/spacerabbit` becomes a thin supervisor, tray host, updater, permissions shell, and settings UI

The goal is to move hot paths and platform-specific behavior into the C++ codebase without forcing a rewrite of the existing GUI, installer, or app lifecycle work that Tauri already handles.

## Current State

Today the Rust app owns almost all runtime behavior:

- global event tap setup in [`app.rs`](/Users/joe/work/spacerabbit/src-tauri/src/app.rs)
- gesture interception, fast swipe, hotkeys, tray scroll, and Mission Control/Expose handling in [`event_handler.rs`](/Users/joe/work/spacerabbit/src-tauri/src/event_handler.rs)
- display switching and window focus logic in [`display.rs`](/Users/joe/work/spacerabbit/src-tauri/src/display.rs)
- settings and config file management in [`settings.rs`](/Users/joe/work/spacerabbit/src-tauri/src/settings.rs) and [`config.rs`](/Users/joe/work/spacerabbit/src-tauri/src/config.rs)
- tray/menu bar behavior in [`tray.rs`](/Users/joe/work/spacerabbit/src-tauri/src/tray.rs)
- stats and telemetry in [`stats.rs`](/Users/joe/work/spacerabbit/src-tauri/src/stats.rs) and [`telemetry.rs`](/Users/joe/work/spacerabbit/src-tauri/src/telemetry.rs)

The C++ repo already has useful building blocks:

- a library surface for Core Graphics, AppKit, AX, CF, dispatch, CGS/SkyLight, and synthetic gestures under [`include/spacerabbit/`](/Users/joe/work/spacerabbit-core/include/spacerabbit)
- a working fast-swipe example in [`examples/fast_swipe.cpp`](/Users/joe/work/spacerabbit-core/examples/fast_swipe.cpp)
- a placeholder daemon binary in [`src/spacerabbitd/main.cpp`](/Users/joe/work/spacerabbit-core/src/spacerabbitd/main.cpp)

## Goals

- Move latency-sensitive runtime behavior into the C++ daemon.
- Keep the existing Rust/Tauri GUI for editing settings.
- Preserve the existing JSON settings file path and schema as much as possible.
- Keep the menu bar app, updater, and permission UX in Rust.
- Make the Rust app supervise the daemon lifecycle.
- Add telemetry to the daemon because that is where the real runtime actions will happen.

## Non-Goals

- Rewriting the GUI in C++.
- Replacing Tauri packaging, updater, or window management immediately.
- Designing a large RPC/control plane before the daemon can do useful work.
- Perfect hot reload in v1 of the split architecture.

## Recommended Architecture

### Process Model

Two long-running processes:

1. `SpaceRabbit.app` (Rust/Tauri)
2. `spacerabbitd` (C++)

Rust becomes the parent supervisor. Its responsibilities:

- start `spacerabbitd`
- restart it if it exits unexpectedly
- stop it cleanly on app quit
- expose the settings UI
- host the tray/menu bar
- handle updater flows and onboarding permission windows

C++ becomes the runtime engine. Its responsibilities:

- install the event tap
- implement fast swipe
- implement hotkeys
- implement tray-scroll behavior if that feature stays
- implement display switching and window-focus behavior
- own stats collection for runtime actions
- send telemetry

### Ownership Split

Rust/Tauri should own:

- settings UI
- config file writes
- permissions UI and deep links into System Settings
- updater
- tray menu items such as Settings, Check for Updates, Quit
- daemon lifecycle supervision

C++ daemon should own:

- event interception
- gesture synthesis
- workspace switching
- display switching
- window focus operations
- action counting and usage metrics
- runtime interpretation of `settings.json`

## Key Design Decision: Keep Restart Logic In The Supervisor

The simplest restart story is:

- Rust watches `settings.json`
- Rust stops and restarts `spacerabbitd` when the file changes
- C++ only needs startup config loading, not self-reexec logic

This is simpler than having the daemon watch the file and restart itself because:

- there is one clear owner of process lifecycle
- crash recovery and config reload use the same code path
- telemetry can distinguish crash restart vs config restart
- the daemon stays focused on runtime behavior, not supervision

If a full restart proves too heavy later, the same architecture can grow a `reload-config` control message without changing the process boundary.

## Config Strategy

### Decision

Keep the current config location and JSON format initially.

Current Rust code resolves config under the existing SpaceRabbit config directory and reads:

- `settings.json`
- `stats.json`

The daemon should read the same `settings.json` path so the GUI does not need a migration at the same time as the runtime rewrite.

### Initial Requirements

- Rust remains the only writer of `settings.json`.
- C++ is the primary reader of `settings.json`.
- Rust should not translate the config into another format before launching the daemon.
- Unknown fields should be tolerated so the schema can evolve gradually.

### Suggested Next Step

Define a shared schema document in `spacerabbit-core/docs/` and keep the Rust and C++ parsers aligned against that schema instead of letting each side drift independently.

## IPC Strategy

### Initial Design

Keep IPC narrow. Do not build general-purpose RPC first.

Required signals from Rust to daemon:

- start with config path
- stop

Required signals from daemon to Rust:

- optional health/status
- optional current desktop number
- optional log forwarding

### Transport

For the first version, stdout/stderr plus process exit codes are enough for supervision. If Rust needs live status, add one local IPC channel:

- Unix domain socket, or
- newline-delimited JSON over a local pipe

The daemon should not depend on Tauri APIs directly.

## Menu Bar Desktop Number

This is the main place where either side could reasonably own the logic.

### Recommendation

Phase 1: keep the tray in Rust and keep desktop-number observation in Rust.

Rationale:

- Rust already owns the tray in [`tray.rs`](/Users/joe/work/spacerabbit/src-tauri/src/tray.rs)
- Rust already has working active-space observation in [`app.rs`](/Users/joe/work/spacerabbit/src-tauri/src/app.rs)
- this avoids introducing daemon-to-tray IPC on day one

This does duplicate a small amount of state observation, but it is operationally simple.

### Future Option

If duplicated space tracking becomes fragile, move to a daemon status event:

- daemon emits `current_space_changed`
- Rust updates the tray title from daemon state

That would make the daemon the runtime source of truth, but it is not required for the first migration.

## Hotkeys

Hotkeys should move fully into the C++ daemon.

Reasons:

- they are part of the same event tap pipeline as fast swipe
- they share config parsing with other runtime behavior
- they should not depend on Tauri state

The Rust side should still support:

- editing hotkey settings in the GUI
- temporarily suspending capture in the GUI while recording a new shortcut, if needed

For the first migration, the simplest way to support hotkey recording is:

- Rust updates `settings.json`
- Rust restarts the daemon

This avoids a live rebind protocol initially.

## Telemetry and Stats

Telemetry should move with the behavior that generates the events.

### Recommendation

The daemon should own:

- incrementing action counters
- writing `stats.json`
- periodic usage report generation
- crash/abnormal-exit classification where possible

Rust should own:

- the telemetry consent toggle in settings
- displaying stats in the GUI

### Why

Once hotkeys and gestures move to C++, the Rust process no longer sees the runtime actions accurately enough to remain the source of telemetry truth.

### Data Compatibility

Keep the existing `stats.json` shape initially so the current GUI can continue reading it with minimal or no changes.

## Permissions

Rust should continue to own the onboarding experience, but the daemon must validate permissions at startup.

Recommended behavior:

- Rust checks permissions before first launch of the daemon
- daemon re-checks permissions on startup and logs a clear failure if unavailable
- if permissions are missing, Rust shows the existing permission UI and either delays startup or supervises a daemon that exits with a known code

This keeps the UX in one place while keeping the daemon safe to run standalone during development.

## Logging

Keep logs per process.

- Rust logs supervision, UI, updater, and permission flows
- C++ logs runtime and system integration behavior

Phase 1 can rely on separate log files. Later, Rust may surface daemon logs in the GUI or collect them into the same support bundle.

## Suggested Daemon Structure

`spacerabbitd` should be split into small subsystems instead of one large event-loop file:

- `config`: parse `settings.json` into a typed runtime config
- `runtime`: daemon startup and shutdown orchestration
- `event_tap`: install tap and dispatch events
- `hotkeys`: key matching and action execution
- `gestures`: fast swipe and Mission Control/Expose gesture synthesis
- `spaces`: current display/current space queries and navigation rules
- `displays`: display cycling and activation
- `windows`: focus restoration and top-window selection
- `stats`: local counter aggregation and `stats.json` writes
- `telemetry`: Sentry or equivalent event submission
- `ipc` or `status`: optional status stream to the supervisor

## Rollout Plan

### Phase 0: Freeze Interfaces

- document the shared config schema
- document daemon exit codes
- define the minimal Rust supervisor contract

### Phase 1: Supervisor + Fast Swipe

- Rust launches `spacerabbitd`
- C++ daemon owns event tap startup
- C++ daemon handles fast swipe
- Rust still owns tray number updates, updater, permissions UI, and settings UI

This gives an immediate end-to-end win with the functionality that already exists in C++ examples.

### Phase 2: Hotkeys

- move hotkey parsing and registration logic into C++
- migrate workspace switching, Mission Control, Expose, and display switching into C++
- use supervisor restart on config change instead of live hotkey reload

### Phase 3: Stats + Telemetry

- move runtime stats writes into C++
- move usage metric emission into C++
- keep Rust reading the same stats file for UI

### Phase 4: Optional Status IPC

- add daemon-to-Rust status updates only if needed
- candidates: current space, permission failures, heartbeat, daemon version, recent errors

## Failure Model

The supervisor should distinguish:

- clean stop requested by Rust
- config-change restart requested by Rust
- daemon startup failure
- daemon crash

Recommended daemon exit-code classes:

- `0`: clean exit
- dedicated non-zero code for missing permissions
- dedicated non-zero code for config parse failure
- other non-zero: crash or unhandled failure

Rust can use these to decide whether to restart immediately, show an error, or open the permissions flow.

## Risks

- The private API behavior used for synthetic gestures and Space inspection remains the main platform risk regardless of language.
- Splitting ownership across two processes can create duplicated state unless the boundary stays narrow.
- Self-restarting daemon logic would complicate crash analysis and supervision; this is why the supervisor-owned restart path is recommended.
- Telemetry must not drift from user consent; the daemon must read the same config and honor opt-out immediately after restart.

## Open Questions

- Should the daemon support a standalone developer mode outside Tauri, or should Rust always be the launcher in production and development?
- Should the tray-scroll feature move to C++ in the first hotkey migration, or be dropped if it adds too much event complexity?
- Do we need live daemon status in the GUI, or are logs plus restart behavior enough initially?
- Should telemetry stay on Sentry for both processes, or should Rust and C++ use separate projects/releases for clearer signal?

## Recommended First Implementation Slice

Build the smallest split that proves the architecture:

1. Rust launches and supervises `spacerabbitd`.
2. C++ daemon loads the existing `settings.json`.
3. C++ daemon installs the event tap and handles fast swipe.
4. Rust keeps the tray number, updater, permissions UI, and settings UI unchanged.
5. Rust restarts the daemon when `settings.json` changes.

That slice validates the new boundary without forcing hotkeys, tray IPC, telemetry, and config migration to land at the same time.
