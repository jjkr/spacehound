# SpaceRabbit gRPC IPC Design

## Summary

This document defines the local IPC contract between the macOS app process and
`spacerabbitd`.

The chosen transport is:

- gRPC
- over a Unix domain socket
- with the daemon as the single runtime authority

This design assumes:

- the app supervises daemon launch and shutdown
- the daemon owns runtime behavior and runtime state
- the app owns UI, menu bar, onboarding, settings editing, and supervision
- `spacerabbitctl` moves onto the same IPC surface instead of bypassing the daemon

The immediate driver is menu bar display of the current Space number, but the
interface is intentionally broad enough to cover config reload, daemon enable or
disable, runtime commands, and future diagnostics.

## Goals

- Keep all runtime Space and display tracking in the daemon.
- Give the app a stable way to fetch current daemon state and subscribe to changes.
- Support one-off commands from the app and `spacerabbitctl`.
- Keep the IPC contract narrow and versioned.
- Preserve a clean path from the current foreground daemon to a more mature
  service architecture later.

## Non-Goals

- Remote or network-accessible RPC.
- Multi-user access.
- Full general-purpose RPC for every internal daemon subsystem.
- Replacing the existing settings file with a live object graph.
- Solving updater, installer, or privilege-boundary packaging in this document.

## Core Decisions

### 1. The daemon is the source of truth

The daemon owns:

- active Space observation
- active display observation
- runtime permission state
- whether runtime capture is enabled
- execution of control actions

The app consumes daemon state. It should not separately reimplement Space
tracking once this IPC surface exists.

### 2. Use unary RPCs plus one server-stream

v1 should use:

- unary RPCs for command and snapshot requests
- one long-lived server-stream for event delivery

There is no need for bidirectional streaming in v1.

### 3. Keep the protocol versioned from day one

The protobuf package should be versioned:

`spacerabbit.daemon.v1`

Breaking changes require a new package version and new service name. Additive
changes stay within `v1`.

### 4. Use a shared C++ client library with an Objective-C++ bridge

The daemon is C++. The app is Objective-C. The simplest implementation is:

- generate C++ gRPC stubs from one shared `.proto`
- build a small C++ client library
- expose that client to the app through an Objective-C++ wrapper

This avoids maintaining separate Objective-C-specific gRPC bindings unless there
is a strong reason to do so later.

## Process Model

The app launches `spacerabbitd` with:

- settings path
- socket path
- token file path

Example launch shape:

```text
spacerabbitd \
  --settings /Users/joe/Library/Application Support/SpaceRabbit/settings.json \
  --ipc-socket /tmp/spacerabbit-501/daemon.sock \
  --ipc-token-file /tmp/spacerabbit-501/session.token
```

The app then:

1. waits for the socket to appear
2. opens a gRPC channel to the Unix socket
3. calls `GetStatus`
4. starts `WatchEvents`
5. updates the menu bar and UI from daemon state

## Transport

### Socket path

Use a per-user runtime directory under `/tmp` with mode `0700`.

Recommended directory:

`/tmp/spacerabbit-<uid>/`

Recommended socket path:

`/tmp/spacerabbit-<uid>/daemon.sock`

Recommended token path:

`/tmp/spacerabbit-<uid>/session.token`

Rationale:

- short path, safe for Unix socket path length limits on macOS
- per-user isolation
- easy cleanup
- stable reconnect target for the supervising app and `spacerabbitctl`

### gRPC target string

Clients should connect with:

`unix:///tmp/spacerabbit-<uid>/daemon.sock`

### Startup and stale socket handling

Daemon startup must:

1. create the runtime directory if missing
2. set directory permissions to `0700`
3. remove any stale socket at the target path
4. bind the gRPC server
5. publish readiness only after the server is listening

Daemon shutdown must unlink the socket path.

## Authentication and Local Trust

The socket is local-only, but the path itself is not a security boundary.

v1 authentication should be:

- app generates a random 128-bit or 256-bit session token on launch
- app writes it to the token file with mode `0600`
- daemon reads that token at startup
- every RPC must include metadata header `x-spacerabbit-session-token`
- daemon rejects missing or invalid tokens with `UNAUTHENTICATED`

This should be used together with the private per-user runtime directory.

Reasons to prefer a token over path secrecy alone:

- explicit authentication at the application layer
- works cleanly with gRPC interceptors
- avoids relying on lower-level Unix peer-credential APIs through gRPC internals

If the daemon later becomes a launchd-managed service, revisit this and consider
XPC or peer credential validation.

## High-Level Interface

The daemon exports one service:

- `SpaceRabbitDaemon`

It supports:

- snapshot reads
- event subscription
- runtime commands
- config reload
- enable or disable
- graceful shutdown

### RPC surface

- `GetStatus`
- `WatchEvents`
- `ExecuteAction`
- `ReloadConfig`
- `SetEnabled`
- `Shutdown`

## Proposed Proto Schema

Suggested file path:

`proto/spacerabbit/daemon/v1/daemon.proto`

```proto
syntax = "proto3";

package spacerabbit.daemon.v1;

import "google/protobuf/timestamp.proto";

service SpaceRabbitDaemon {
  rpc GetStatus(GetStatusRequest) returns (GetStatusResponse);
  rpc WatchEvents(WatchEventsRequest) returns (stream DaemonEvent);
  rpc ExecuteAction(ExecuteActionRequest) returns (ExecuteActionResponse);
  rpc ReloadConfig(ReloadConfigRequest) returns (ReloadConfigResponse);
  rpc SetEnabled(SetEnabledRequest) returns (SetEnabledResponse);
  rpc Shutdown(ShutdownRequest) returns (ShutdownResponse);
}

message GetStatusRequest {}

message GetStatusResponse {
  RuntimeStatus status = 1;
}

message WatchEventsRequest {
  bool include_snapshot = 1;
}

message DaemonEvent {
  uint64 sequence = 1;
  google.protobuf.Timestamp occurred_at = 2;

  oneof event {
    SnapshotEvent snapshot = 10;
    SpaceChangedEvent space_changed = 11;
    PermissionChangedEvent permission_changed = 12;
    EnabledChangedEvent enabled_changed = 13;
    ConfigReloadedEvent config_reloaded = 14;
    HealthChangedEvent health_changed = 15;
    ShuttingDownEvent shutting_down = 16;
  }
}

message SnapshotEvent {
  RuntimeStatus status = 1;
}

message SpaceChangedEvent {
  SpaceSnapshot current_space = 1;
}

message PermissionChangedEvent {
  PermissionSnapshot permissions = 1;
}

message EnabledChangedEvent {
  bool enabled = 1;
  string reason = 2;
}

message ConfigReloadedEvent {
  ConfigSnapshot config = 1;
}

message HealthChangedEvent {
  RuntimeHealth health = 1;
  string message = 2;
}

message ShuttingDownEvent {
  string reason = 1;
}

message ExecuteActionRequest {
  string request_id = 1;

  oneof action {
    WorkspaceActionRequest workspace = 10;
    DisplayActionRequest display = 11;
    WindowFocusActionRequest window_focus = 12;
    SystemUiActionRequest system_ui = 13;
  }
}

message ExecuteActionResponse {
  ActionDisposition disposition = 1;
  RuntimeStatus status = 2;
}

message ReloadConfigRequest {}

message ReloadConfigResponse {
  RuntimeStatus status = 1;
}

message SetEnabledRequest {
  bool enabled = 1;
  string reason = 2;
}

message SetEnabledResponse {
  RuntimeStatus status = 1;
}

message ShutdownRequest {
  string reason = 1;
}

message ShutdownResponse {}

message RuntimeStatus {
  ProtocolVersion protocol_version = 1;
  string daemon_version = 2;
  uint32 daemon_pid = 3;
  RuntimeHealth health = 4;
  bool enabled = 5;
  PermissionSnapshot permissions = 6;
  SpaceSnapshot current_space = 7;
  ConfigSnapshot config = 8;
  uint64 last_event_sequence = 9;
  repeated Capability capabilities = 10;
}

message ProtocolVersion {
  uint32 major = 1;
  uint32 minor = 2;
}

message PermissionSnapshot {
  bool accessibility_trusted = 1;
  bool input_monitoring_granted = 2;
}

message SpaceSnapshot {
  string active_display_uuid = 1;
  uint32 active_display_ordinal = 2;
  uint64 managed_space_id = 3;
  uint32 space_ordinal = 4;
  uint32 space_count = 5;
}

message ConfigSnapshot {
  string settings_path = 1;
  uint64 generation = 2;
  google.protobuf.Timestamp loaded_at = 3;
}

message WorkspaceActionRequest {
  WorkspaceAction action = 1;
  uint32 target_space_ordinal = 2;
  bool wrap = 3;
}

message DisplayActionRequest {
  DisplayAction action = 1;
  uint32 target_display_ordinal = 2;
  bool wrap = 3;
}

message WindowFocusActionRequest {
  WindowFocusDirection direction = 1;
}

message SystemUiActionRequest {
  SystemUiElement element = 1;
}

enum RuntimeHealth {
  RUNTIME_HEALTH_UNSPECIFIED = 0;
  RUNTIME_HEALTH_STARTING = 1;
  RUNTIME_HEALTH_RUNNING = 2;
  RUNTIME_HEALTH_DEGRADED = 3;
  RUNTIME_HEALTH_STOPPING = 4;
}

enum Capability {
  CAPABILITY_UNSPECIFIED = 0;
  CAPABILITY_GET_STATUS = 1;
  CAPABILITY_WATCH_EVENTS = 2;
  CAPABILITY_EXECUTE_ACTION = 3;
  CAPABILITY_RELOAD_CONFIG = 4;
  CAPABILITY_SET_ENABLED = 5;
  CAPABILITY_SHUTDOWN = 6;
}

enum ActionDisposition {
  ACTION_DISPOSITION_UNSPECIFIED = 0;
  ACTION_DISPOSITION_APPLIED = 1;
  ACTION_DISPOSITION_NOOP = 2;
}

enum WorkspaceAction {
  WORKSPACE_ACTION_UNSPECIFIED = 0;
  WORKSPACE_ACTION_LEFT = 1;
  WORKSPACE_ACTION_RIGHT = 2;
  WORKSPACE_ACTION_GOTO = 3;
}

enum DisplayAction {
  DISPLAY_ACTION_UNSPECIFIED = 0;
  DISPLAY_ACTION_LEFT = 1;
  DISPLAY_ACTION_RIGHT = 2;
  DISPLAY_ACTION_GOTO = 3;
}

enum WindowFocusDirection {
  WINDOW_FOCUS_DIRECTION_UNSPECIFIED = 0;
  WINDOW_FOCUS_DIRECTION_NEXT = 1;
  WINDOW_FOCUS_DIRECTION_PREVIOUS = 2;
}

enum SystemUiElement {
  SYSTEM_UI_ELEMENT_UNSPECIFIED = 0;
  SYSTEM_UI_ELEMENT_MISSION_CONTROL = 1;
  SYSTEM_UI_ELEMENT_EXPOSE = 2;
}
```

## Semantics

### `GetStatus`

Returns the daemon's current snapshot.

This is used by:

- app startup
- reconnect after daemon restart
- debugging
- `spacerabbitctl status` if that command is added later

The response should always be self-contained. The app must be able to build its
entire menu bar state from `RuntimeStatus`.

### `WatchEvents`

`WatchEvents` is the long-lived state subscription stream.

Rules:

- if `include_snapshot` is true, the first event must be `SnapshotEvent`
- events must be emitted in strictly increasing `sequence` order
- the stream must end if the daemon shuts down or the channel breaks
- clients should reconnect and call `GetStatus` again if the stream ends

This stream is the app's primary update path for:

- current Space number
- permission changes
- enable or disable changes
- reload status
- daemon health changes

### `ExecuteAction`

This is the daemon-backed replacement for direct control execution.

The daemon should internally map these requests onto the same logical actions
currently represented by `spacerabbit::control::request`.

This keeps:

- telemetry in one place
- state transitions coherent
- future hotkey and UI actions on the same path

`ExecuteActionResponse.disposition` values:

- `APPLIED`: action executed
- `NOOP`: action was valid but produced no change

Application failures should use non-OK gRPC status codes instead of encoding
errors in the response body.

### `ReloadConfig`

Forces the daemon to reread `settings.json` without full process restart.

This should be the first config reload path once IPC exists. The supervising app
can still choose to restart the daemon on major config changes, but the protocol
should support in-process reload immediately.

### `SetEnabled`

Controls whether runtime capture and action execution are active.

This is useful for:

- a tray toggle
- disabling behavior while recording shortcuts
- onboarding and permission flows
- temporary emergency disable

When `enabled` is false:

- event interception should be inactive or inert
- state observation should continue
- `GetStatus` and `WatchEvents` remain available

### `Shutdown`

Requests graceful daemon termination.

Expected behavior:

1. daemon emits `ShuttingDownEvent`
2. daemon stops accepting new work
3. daemon tears down event taps and resources
4. daemon exits cleanly

The app still remains the lifecycle owner.

## External State Semantics

### Space numbering

The app wants a menu bar number. The daemon should publish:

- `space_ordinal`: 1-based index of the current Space on the active display
- `space_count`: number of Spaces on the active display

The daemon should also publish:

- `managed_space_id`: stable opaque identifier for the current Space
- `active_display_uuid`: stable display identity

UI code should display `space_ordinal` and use the identifiers only for
debugging and change detection.

### Unknown or unavailable Space state

If the daemon cannot determine current Space state, it should:

- leave `current_space` unset in `RuntimeStatus`, or
- omit `current_space` in an event payload
- set `health` to `DEGRADED` if the failure is persistent

The app should render a placeholder instead of stale data in that case.

### Permission state

The daemon should publish at least:

- Accessibility permission state
- Input Monitoring permission state

The daemon already depends on Accessibility for runtime actions. Including both
permissions in status makes the app's menu bar and onboarding UI simpler.

## Error Model

Use gRPC status codes for application-visible failures.

Recommended mapping from current internal error categories:

- `permission_denied` -> `PERMISSION_DENIED`
- `invalid_request` -> `INVALID_ARGUMENT`
- `state_unavailable` -> `FAILED_PRECONDITION`
- `runtime_error` -> `INTERNAL`

Other status guidance:

- invalid or missing session token -> `UNAUTHENTICATED`
- daemon not ready yet -> `UNAVAILABLE`
- unsupported future RPC on older daemon -> `UNIMPLEMENTED`

`NOOP` is not an error and should return `OK`.

## Event Emission Rules

The daemon should only emit change events when the externally visible state
actually changed.

For `SpaceChangedEvent`, emit when any of these change:

- `managed_space_id`
- `space_ordinal`
- `space_count`
- `active_display_uuid`
- `active_display_ordinal`

This avoids redundant menu bar churn.

The event stream should be fed from a daemon-owned status store:

- runtime code updates the status store
- the store compares old and new snapshots
- the store broadcasts typed events to stream subscribers

This is simpler than having each subsystem directly push gRPC events.

## Objective-C App Integration

The app side should expose a narrow Objective-C-friendly API, implemented in
Objective-C++ on top of the shared C++ gRPC client.

Suggested wrapper shape:

```objc
@interface SRDaemonClient : NSObject
- (instancetype)initWithSocketPath:(NSString *)socketPath
                         tokenPath:(NSString *)tokenPath;
- (void)connectWithCompletion:(void (^)(NSError * _Nullable error))completion;
- (void)fetchStatusWithCompletion:(void (^)(SRRuntimeStatus * _Nullable status,
                                           NSError * _Nullable error))completion;
- (void)startWatchingWithHandler:(void (^)(SRDaemonEvent *event))handler
                      completion:(void (^)(NSError * _Nullable error))completion;
- (void)executeAction:(SRActionRequest *)request
           completion:(void (^)(SRActionResult * _Nullable result,
                                NSError * _Nullable error))completion;
- (void)reloadConfigWithCompletion:(void (^)(SRRuntimeStatus * _Nullable status,
                                             NSError * _Nullable error))completion;
- (void)setEnabled:(BOOL)enabled
             reason:(NSString *)reason
         completion:(void (^)(SRRuntimeStatus * _Nullable status,
                              NSError * _Nullable error))completion;
@end
```

The wrapper should:

- hide protobuf and gRPC details from the rest of the app
- dispatch callbacks onto the app's chosen queue, usually the main queue
- reconnect cleanly when the daemon restarts

## `spacerabbitctl` Integration

`spacerabbitctl` should stop calling `spacerabbit::control::execute` directly and
should instead become a gRPC client of the daemon.

Reasons:

- one runtime authority
- action telemetry remains in the daemon
- CLI behavior matches app behavior
- future daemon-only state can influence action handling

The direct library entry point can still exist for testing or standalone
examples, but the user-facing CLI should go through IPC.

## Daemon Internals

The daemon should have three relevant internal layers:

1. runtime engine
2. shared status store
3. gRPC facade

### Runtime engine

Owns:

- event taps
- gesture handling
- display and Space observation
- config parsing and reload
- action execution

### Shared status store

Owns:

- current `RuntimeStatus`
- event sequence counter
- diffing and event generation
- subscription fan-out

The status store should be thread-safe and independent of the transport.

### gRPC facade

Owns:

- server startup and shutdown
- auth token interceptor
- request-to-runtime mapping
- event stream subscriber lifetimes

This keeps gRPC out of the low-latency runtime core.

## Versioning Rules

Within `spacerabbit.daemon.v1`:

- only add fields
- never reuse field numbers
- reserve removed field numbers and enum values
- keep old fields semantically valid

Breaking changes require:

- `spacerabbit.daemon.v2`
- new service name if needed
- app and daemon compatibility gating by protocol major version

`RuntimeStatus.protocol_version` should report the server's protocol major and
minor versions so the client can fail clearly on mismatch.

## Build and Codegen Plan

Recommended repository additions:

- `proto/spacerabbit/daemon/v1/daemon.proto`
- CMake target for protobuf and gRPC code generation
- `spacerabbit_ipc_proto` static library for generated C++ code
- `spacerabbit_ipc_client` static library for reusable client code
- daemon target linked against `spacerabbit_ipc_proto`
- `spacerabbitctl` linked against `spacerabbit_ipc_client`

The Objective-C app should consume the C++ client through an Objective-C++
wrapper rather than generating another client stack.

## Rollout Plan

### Phase 1: transport and status only

- add socket startup and auth token handling
- implement `GetStatus`
- implement `WatchEvents`
- publish current Space state from the daemon
- wire menu bar title to daemon events

This is enough to move Space-number ownership into the daemon.

### Phase 2: command path

- implement `ExecuteAction`
- move `spacerabbitctl` to gRPC
- route app-initiated actions through the daemon

### Phase 3: live config and runtime toggles

- implement `ReloadConfig`
- implement `SetEnabled`
- remove daemon restart for ordinary settings edits where practical

### Phase 4: diagnostics

- optionally add log forwarding
- optionally add stats or telemetry inspection RPCs
- optionally add richer health information

## Testing Plan

Add tests for:

- socket startup and stale socket cleanup
- auth token acceptance and rejection
- `GetStatus` snapshot correctness
- `WatchEvents` initial snapshot delivery
- event ordering and monotonic sequence numbers
- Space-change event emission only on real changes
- config reload updating `generation`
- `SetEnabled` toggling runtime state
- gRPC status mapping for action failures
- daemon shutdown semantics
- `spacerabbitctl` end-to-end action execution through IPC

## Open Questions

- Whether the daemon should support replay after a dropped event stream. v1 does
  not require replay; reconnect plus `GetStatus` is sufficient.
- Whether config reload should fully replace supervisor-driven restart for all
  settings classes. The protocol should support reload now even if some changes
  still trigger restart initially.
- Whether the app should expose diagnostics such as current health and last error
  in the menu bar. The interface supports it, but the UI policy is separate.

## Recommendation

Implement this as a versioned gRPC-over-UDS control plane with:

- one `SpaceRabbitDaemon` service
- unary RPCs for command and snapshot calls
- one server-stream for live state changes
- daemon-owned status tracking
- a shared C++ client library bridged into the Objective-C app through
  Objective-C++

That gives you a clean menu bar state path now and a durable local control plane
for the rest of the daemon work without locking you into XPC or an oversized RPC
surface.
