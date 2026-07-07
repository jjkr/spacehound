# SpaceRabbit Native macOS Daemon Design

## Summary

This document proposes a native macOS architecture for `spacerabbitd` based on:

- `launchd` for lifecycle management
- `SMAppService` for helper registration
- XPC for app-to-daemon communication
- explicit code-signing validation and least-privilege boundaries

The goal is to replace the current "app launches a child process and talks over a
custom transport" model with the standard macOS service model.

For SpaceRabbit, the right native shape is:

- `SpaceRabbit.app` remains the UI, settings, onboarding, and menu bar process
- `spacerabbitd` becomes a per-user background agent managed by `launchd`
- app and daemon communicate over a private XPC Mach service
- the daemon hosts a thin Objective-C++ XPC facade over the C++ runtime core

This design targets modern direct distribution on macOS with code signing and
notarization. It is intentionally conservative about privileges and avoids
running as root.

## Why Change the Model

The current supervised-child-process model is functional, but it leaves several
macOS-native capabilities on the table:

- service lifecycle is managed by the app instead of `launchd`
- IPC security is a custom responsibility instead of a first-class OS feature
- crash recovery and reconnection are not using the platform's native patterns
- privilege separation is weaker than the XPC model Apple recommends

Apple's guidance is clear on the broad direction:

- use XPC for privilege separation instead of raw child processes
- use `launchd`-managed helpers instead of ad hoc service installation
- use `SMAppService` on macOS 13 and later to register bundled helpers

## Goals

- Make `spacerabbitd` a first-class macOS background service.
- Use XPC instead of sockets or pipes for local IPC.
- Keep the daemon as the single runtime authority for Space and display state.
- Follow least-privilege and code-signing verification practices.
- Preserve a clean boundary between the Objective-C app and the C++ runtime core.

## Non-Goals

- Running as root.
- Using a system-wide LaunchDaemon.
- Rewriting the runtime engine in Objective-C.
- Building a broad distributed-object system.
- Targeting Mac App Store constraints in this document.

## Core Recommendation

SpaceRabbit should use a per-user `LaunchAgent`, not an embedded XPC service and
not a root `LaunchDaemon`.

### Why not an embedded XPC service

Apple's XPC service model is excellent for isolated helper work, but bundled XPC
services are designed for on-demand launch, abrupt termination when idle, and a
minimal-state model.

That is a poor fit for SpaceRabbit because the daemon needs to:

- maintain long-lived event taps
- observe Space changes continuously
- keep runtime state in memory
- remain present for menu bar updates and hotkey handling

### Why not a LaunchDaemon

SpaceRabbit interacts with:

- the logged-in user's WindowServer session
- Accessibility state
- input-event monitoring
- user-facing desktop and window state

That work belongs in the user's session, not in a root daemon. A root daemon
would expand privilege unnecessarily and complicate access to the user session.

### Why a LaunchAgent

A per-user LaunchAgent is the native fit because it:

- runs in the logged-in user's context
- is managed by `launchd`
- can expose an XPC Mach service
- survives app relaunches
- cleanly separates background runtime behavior from the UI process

## Process Model

Recommended runtime components:

1. `SpaceRabbit.app`
2. `spacerabbitd` LaunchAgent

Responsibilities:

### `SpaceRabbit.app`

- settings UI
- menu bar UI
- onboarding and permissions guidance
- helper registration via `SMAppService`
- reconnect logic for daemon availability
- settings writes

### `spacerabbitd`

- event tap installation
- Space tracking
- display switching
- runtime hotkeys
- telemetry and runtime stats
- action execution
- status publication to the app

## Service Registration and Lifecycle

### Registration API

For macOS 13 and later, register the helper with `SMAppService`.

Use:

- `SMAppService.agent(plistName:)`

Do not install helper plists manually into `~/Library/LaunchAgents`.

### Agent packaging

The app bundle should contain:

- the LaunchAgent plist
- the agent executable or agent bundle
- any helper-local resources required by the daemon

The app is responsible for requesting registration and surfacing status to the
user when macOS requires approval.

### Lifecycle ownership

`launchd` should own daemon lifecycle.

That means:

- the app does not manually `fork`, `exec`, or supervise the daemon
- the app does not restart the daemon directly after a crash
- the app reconnects over XPC when the daemon is available again
- crash restart policy lives with `launchd`

This is the core behavioral shift from the current design.

### LaunchAgent behavior

The LaunchAgent should:

- start in the logged-in user session
- publish a Mach service name through `MachServices`
- be configured for persistent background runtime behavior

For SpaceRabbit's workload, treat the agent as long-running rather than
request-per-launch. `launchd` should still be the owner of restart semantics.

### LaunchAgent plist shape

At minimum, the LaunchAgent plist should define:

- `Label`
- `ProgramArguments`
- `MachServices`
- `RunAtLoad`

Add `KeepAlive` only to the extent needed for the daemon's runtime behavior and
crash recovery policy. Prefer the smallest launch policy that still keeps the
runtime reliable.

## IPC Model

### Transport

Use XPC over a Mach service advertised by the LaunchAgent.

The app should connect with `NSXPCConnection` using the daemon's Mach service
name, not with raw sockets.

Recommended client initializer:

- `initWithMachServiceName:options:`

### Interface style

Use Foundation XPC:

- `NSXPCListener`
- `NSXPCConnection`
- `NSXPCInterface`
- Objective-C protocols with reply blocks

The daemon executable should be Objective-C++ at the XPC boundary so it can host
Foundation APIs while calling directly into the C++ runtime.

### Connection pattern

Use one app-to-daemon control connection per app process.

The client connection should:

- set the daemon remote interface
- set an exported observer interface for callbacks
- set interruption and invalidation handlers
- set a code-signing requirement before `resume`

The server side should:

- accept or reject incoming connections in `NSXPCListenerDelegate`
- apply a code-signing requirement for the app peer
- configure remote and exported interfaces
- track active observer connections

## XPC Interface Design

The public XPC contract should stay narrow and domain-specific.

Recommended exported daemon protocol:

```objc
@protocol SRDaemonXPC

- (void)fetchDaemonStateWithReply:(void (^)(SRDaemonState *state,
                                           NSError *error))reply;

- (void)subscribeWithReply:(void (^)(NSUUID *subscriptionID,
                                    SRDaemonState *initialState,
                                    NSError *error))reply;

- (void)unsubscribe:(NSUUID *)subscriptionID
          withReply:(void (^)(NSError *error))reply;

- (void)executeCommand:(SRCommandRequest *)request
             withReply:(void (^)(SRCommandResult *result,
                                 NSError *error))reply;

- (void)reloadSettingsWithReply:(void (^)(SRDaemonState *state,
                                         NSError *error))reply;

- (void)setDaemonEnabled:(BOOL)enabled
                  reason:(NSString *)reason
               withReply:(void (^)(SRDaemonState *state,
                                   NSError *error))reply;

- (void)shutdownWithReason:(NSString *)reason
                 withReply:(void (^)(NSError *error))reply;

@end
```

Recommended app-exported observer protocol:

```objc
@protocol SRDaemonObserverXPC

- (void)daemonStateDidChange:(SRDaemonState *)state;

- (void)activeSpaceDidChange:(SRActiveSpaceState *)spaceState;

- (void)permissionsDidChange:(SRPermissionState *)permissions;

- (void)daemonWillTerminateWithReason:(NSString *)reason;

@end
```

### Why this shape

This keeps the model simple:

- unary request-reply methods for commands and snapshots
- daemon-to-app callbacks for change notifications
- one shared state object model across all paths

It avoids inventing a custom streaming layer on top of XPC.

## Data Model

All custom XPC objects should be Objective-C classes that conform to
`NSSecureCoding`.

Recommended model classes:

- `SRDaemonState`
- `SRActiveSpaceState`
- `SRPermissionState`
- `SRSettingsState`
- `SRCommandRequest`
- `SRCommandResult`
- `SRDisplayState`

### `SRDaemonState`

Suggested fields:

- daemon version
- daemon PID
- health state
- enabled flag
- permissions snapshot
- active Space snapshot
- settings generation
- capability flags

### `SRActiveSpaceState`

Suggested fields:

- active display UUID
- active display ordinal
- managed Space ID
- Space ordinal
- Space count

The menu bar should render from `Space ordinal` and never infer its own state by
duplicating the daemon's observation logic.

### `SRCommandRequest`

Represent the existing control surface as an explicit domain command:

- workspace left
- workspace right
- workspace goto
- display left
- display right
- display goto
- window next
- window previous
- Mission Control toggle
- Expose toggle

Internally, the daemon can still map these onto the current
`spacerabbit::control::request` variant.

## Security Model

This design aims for the strongest practical macOS-native security posture for
SpaceRabbit's requirements.

### 1. Use `launchd` and XPC instead of a spawned child process

Apple explicitly recommends XPC for privilege separation instead of relying on a
child process spawned by the app.

This removes:

- custom process launch surfaces
- custom socket authentication
- fragile PID-based trust assumptions

### 2. Use a per-user agent, not root

The daemon must run as the user because it needs access to the user's desktop
session and input state.

Security benefit:

- much smaller blast radius than a privileged helper
- no privileged file writes
- no root-level code path for runtime desktop features

### 3. Require code-signing checks on both sides

The app should set a code-signing requirement on the XPC connection before
resuming it.

The daemon should also require that incoming connections match the expected
SpaceRabbit app identity before accepting privileged requests.

Recommended requirement policy:

- exact Team ID
- exact bundle identifier, or a tightly scoped designated requirement

This prevents another local process from successfully impersonating the app even
if it knows the Mach service name.

### 4. Use strict `NSXPCInterface` definitions

Never accept untyped or overly broad interfaces.

For every XPC connection:

- declare the exact Objective-C protocol
- use `NSSecureCoding` for all custom payloads
- call `setClasses:forSelector:argumentIndex:ofReply:` for collection members
- avoid passing arbitrary object graphs

This constrains the decoder surface and reduces deserialization risk.

### 5. Keep the helper privilege-minimal

The daemon should only have the capabilities needed for:

- event taps
- Accessibility actions
- Space tracking
- reading configuration
- writing daemon-owned runtime state if needed

Do not give the daemon network behavior, filesystem reach, or helper roles that
are unrelated to SpaceRabbit runtime work.

### 6. Prefer one writer for settings

The app should remain the canonical writer of `settings.json`.

The daemon should:

- read settings
- reload settings on demand
- never mutate the shared settings file opportunistically

That reduces race surfaces and makes UI-to-daemon state flow easy to reason
about.

### 7. Separate UI and runtime trust boundaries

The app should remain the human-facing process.

The daemon should not:

- show onboarding UI
- own update flows
- open arbitrary windows
- make packaging or registration decisions

This keeps background execution narrow and auditable.

## Sandbox and Distribution Posture

There is an important practical constraint for SpaceRabbit:

- global input monitoring
- Accessibility control
- private Space APIs
- synthetic gesture posting

may limit how far App Sandbox can be applied in practice.

### Recommendation

Treat the native XPC architecture and full App Sandbox adoption as separate
decisions.

Recommended posture:

- sign all components with the same Team ID
- notarize the shipped app bundle and helpers
- sandbox the main app if and only if the product still functions correctly
- sandbox the daemon only if the runtime APIs used by SpaceRabbit continue to
  work correctly under that profile

If the daemon cannot be sandboxed without breaking required runtime behavior, it
should still be:

- bundled
- signed
- notarized
- launchd-managed
- reachable only through a code-validated XPC interface

That is still a materially better security posture than a raw spawned child
process with custom IPC.

This recommendation is an inference from SpaceRabbit's runtime requirements, not
a claim that Apple documents those specific APIs as sandbox-compatible or
incompatible.

## TCC and Permission Ownership

The process that performs protected actions should be the process granted the
relevant user permissions.

For SpaceRabbit, that means `spacerabbitd` should be treated as the owner of:

- Accessibility trust used for window and UI automation
- Input Monitoring access used for global event interception, if required by the
  runtime path

The app can:

- detect missing permissions
- display guidance
- deep-link the user into System Settings when possible

But the daemon should be the process whose identity is associated with the
runtime access it needs.

Operationally, keep the daemon's signing identity, bundle identifier, and install
shape stable across updates so the user is not forced into avoidable permission
re-approval churn.

## State and Notification Model

The daemon should be the single source of truth for:

- active Space
- active display
- daemon enabled state
- health and degraded state
- runtime permissions

The app should:

- fetch initial state after connecting
- subscribe once
- update the menu bar from callbacks
- reconnect and refetch after interruption or invalidation

### Event fan-out

The daemon should maintain an internal observer registry keyed by active XPC
connections or subscription IDs.

When externally visible state changes, it should notify subscribed app clients
through the observer protocol.

The daemon should only emit notifications when state actually changes.

## Failure and Recovery

The app should assume XPC connections can be interrupted or invalidated.

Client requirements:

- set `interruptionHandler`
- set `invalidationHandler`
- reconnect with backoff
- refetch daemon state on reconnect
- resubscribe after reconnect

Daemon requirements:

- treat client connections as ephemeral
- avoid coupling daemon state to a specific app connection
- cleanly drop observer registrations on disconnect
- remain able to serve a later reconnect without a special recovery path

This fits `launchd` and XPC's failure model better than trying to preserve a
long-lived app-supervised socket session.

## Daemon Host Architecture

The clean implementation split is:

1. Objective-C++ host layer
2. C++ runtime engine

### Objective-C++ host layer

Responsibilities:

- `NSXPCListener` setup
- connection acceptance and identity validation
- XPC object encoding and decoding
- observer subscription management
- mapping XPC methods to C++ runtime calls

This code should live in `.mm` files and stay deliberately small.

### C++ runtime engine

Responsibilities:

- event taps
- Space observation
- display actions
- hotkey actions
- state tracking
- config reload
- telemetry

The C++ layer should not know about XPC types directly.

## Suggested Bundle and Identifier Layout

Example naming:

- app bundle ID: `com.spacerabbit.app`
- agent label: `com.spacerabbit.daemon`
- Mach service name: `com.spacerabbit.daemon.xpc`

Suggested packaging:

- app bundle contains the LaunchAgent plist registered by `SMAppService`
- agent executable is bundled with the app
- all components share one Team ID

Use stable identifiers. Avoid renaming the daemon helper casually after shipping.

## Suggested LaunchAgent Policy

The LaunchAgent plist should be designed for:

- per-user startup
- publication of the daemon's Mach service
- clean restart on crash
- no unnecessary environment inheritance

Avoid:

- shell wrappers
- custom bootstrap scripts
- manual `launchctl` installation from the app
- using the UI process as the daemon's parent-of-record

## Command Surface

Keep the XPC command surface narrow.

Recommended v1 command set:

- fetch daemon state
- subscribe and unsubscribe
- execute command
- reload settings
- set enabled
- shutdown

Possible future additions:

- request diagnostics snapshot
- fetch recent runtime log messages
- request a permission recheck

Do not expose internal subsystems directly over XPC unless the UI has a concrete
need for them.

## `spacerabbitctl` Strategy

If `spacerabbitctl` remains a user-facing CLI, it should not bypass the daemon.

Preferred options:

1. make `spacerabbitctl` an XPC client of the LaunchAgent
2. keep `spacerabbitctl` as a development-only direct library harness

For end-user behavior, option 1 is better because it preserves:

- one runtime authority
- one permission identity
- one telemetry path
- one consistent action path

If `spacerabbitctl` becomes an XPC client, apply the same code-signing policy to
it or decide explicitly that the CLI is a development tool only.

## Rollout Plan

### Phase 1: native host shell

- add a LaunchAgent target for `spacerabbitd`
- register it through `SMAppService`
- stand up a minimal XPC listener
- implement `fetchDaemonState`
- connect from the app with `NSXPCConnection`

### Phase 2: subscriptions

- add observer callbacks
- push active Space changes from the daemon
- drive the menu bar title from daemon notifications

### Phase 3: commands

- add `executeCommand`
- route app-initiated runtime actions through XPC
- migrate `spacerabbitctl` if desired

### Phase 4: config reload and health

- add `reloadSettings`
- add `setDaemonEnabled`
- surface degraded-health state and permission changes

### Phase 5: tighten security and packaging

- finalize code-signing requirements
- validate notarized update behavior
- decide final sandbox posture per target

## Testing Plan

Add tests for:

- `SMAppService` registration and status handling
- XPC connection establishment
- code-signing requirement failures
- daemon rejection of unexpected clients
- `NSSecureCoding` round-trip for all custom model objects
- collection whitelist enforcement on `NSXPCInterface`
- reconnect after daemon interruption or invalidation
- menu bar updates after active Space changes
- config reload behavior
- daemon crash and `launchd` restart behavior

Also add manual verification for:

- TCC prompts and onboarding flow
- daemon behavior across logout and login
- helper update and replacement across app upgrades
- stability of permissions across signed updates

## References

Primary Apple documentation consulted for this design:

- `SMAppService`
  https://developer.apple.com/documentation/servicemanagement/smappservice
- `Service Management`
  https://developer.apple.com/documentation/servicemanagement/
- `Creating XPC services`
  https://developer.apple.com/documentation/xpc/creating-xpc-services
- `Creating XPC Services`
  https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPSystemStartup/Chapters/CreatingXPCServices.html
- `Creating Launch Daemons and Agents`
  https://developer.apple.com/library/archive/documentation/MacOSX/Conceptual/BPSystemStartup/Chapters/CreatingLaunchdJobs.html
- `setCodeSigningRequirement:`
  https://developer.apple.com/documentation/foundation/nsxpcconnection/setcodesigningrequirement%28_%3A%29
- `setClasses:forSelector:argumentIndex:ofReply:`
  https://developer.apple.com/documentation/foundation/nsxpcinterface/setclasses%28_%3Afor%3Aargumentindex%3Aofreply%3A%29
- `Enabling App Sandbox`
  https://developer.apple.com/library/archive/documentation/Miscellaneous/Reference/EntitlementKeyReference/Chapters/EnablingAppSandbox.html

## Recommendation

For a native macOS architecture, SpaceRabbit should move to:

- a bundled per-user LaunchAgent registered with `SMAppService`
- XPC over a Mach service
- an Objective-C++ XPC host layered over the C++ runtime
- code-signing validation on both sides of the connection
- least-privilege distribution with no root daemon

That is the most Apple-native way to run `spacerabbitd` while keeping the
runtime engine in C++ and materially improving the security and lifecycle story
over a custom spawned-child-process design.
