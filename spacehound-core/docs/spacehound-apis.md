# SpaceHound macOS API Inventory

This document inventories the macOS APIs used by SpaceHound, based on the current native app and C++ runtime code in:

- `SpaceHoundApp/Sources/`
- `spacehound-core/include/`
- `spacehound-core/lib/`

Scope notes:

- This document focuses on the app's current runtime/backend code paths.
- It excludes `examples/` and older design notes, except where behavior has since been promoted into the internal `spacehound` C++ runtime surface.
- Where the codebase contains extra wrappers that are compiled but not used by the normal app flow, those are called out separately.

## High-Level Summary

SpaceHound depends on a small set of macOS frameworks to do four things:

1. Intercept system input and synthesize replacement input, especially trackpad-like gesture events for Space switching.
2. Read macOS display and window state so it knows which display, Space, and window is active.
3. Use Accessibility APIs to inspect the Dock's Mission Control UI and to raise/focus windows.
4. Listen for active Space changes so the tray title stays in sync.

The app uses both public and private macOS APIs:

- Public APIs: Accessibility (`AX*`), AppKit (`NSWorkspace`, `NSRunningApplication`, `NSScreen`), Core Graphics (`CG*`), Core Foundation (`CF*`), and Grand Central Dispatch.
- Private/undocumented APIs: CGS/SkyLight Space APIs and private `CGEvent` gesture fields. These are central to how SpaceHound switches Spaces quickly.
- In this repo, the C++ runtime exposes synthetic gesture construction and posting through `spacehound::gesture`, built on those same private `CGEvent` fields.

## Framework Overview

| Framework / area | Public? | What SpaceHound uses it for |
| --- | --- | --- |
| Accessibility / ApplicationServices | Public | Permission checks, reading Dock accessibility hierarchy, focusing and raising windows |
| Core Graphics / Quartz Event Services | Mostly public | Event taps, event posting, display enumeration, window listing, cursor warping |
| AppKit | Public | Active Space notifications, app activation, main-screen scale factor |
| Core Foundation | Public | Run loop integration, dictionaries/arrays/numbers/strings returned by system APIs |
| libdispatch / GCD | Public | Dispatching window-focus work back to the main thread |
| CGS / SkyLight | Private | Reading Space metadata and active-display state |
| Undocumented CGEvent fields | Private | Constructing synthetic gesture events that behave like Space/Mission Control swipes |

## Public APIs Used In The Main App Flow

### 1. Accessibility / ApplicationServices

These APIs are used for permission checks, window focusing, and reading Mission Control/Expose state from the Dock.

#### Permission APIs

- `AXIsProcessTrusted()`
  - Checks whether the app already has Accessibility permission.
  - Used for safe polling from the setup UI and startup checks.

- `AXIsProcessTrustedWithOptions(...)`
  - Same basic check, but can request the system permission prompt when passed `AXTrustedCheckOptionPrompt`.
  - Used when the user asks the app to request permissions.

#### Window and UI element APIs

- `AXUIElementCreateApplication(pid)`
  - Creates an Accessibility object for a target app.
  - Used when SpaceHound focuses a specific window in another app.

- `AXUIElementCopyAttributeValue(...)`
  - Reads AX attributes such as window lists, focused window, title, position, size, and Dock hierarchy information.
  - This is one of the most heavily used AX calls in the app.

- `AXUIElementSetAttributeValue(...)`
  - Writes AX attributes such as `AXFocusedWindow` and `AXMain`.
  - Used to make a target window the main/focused window after raising it.

- `AXUIElementPerformAction(...)`
  - Performs AX actions such as `AXRaise`.
  - Used to bring a window to the front.

- `AXValueGetValue(...)`
  - Decodes typed AX values such as `CGPoint`, `CGSize`, and `CGRect`.
  - Used to extract window bounds and Mission Control thumbnail bounds.

#### AX attributes and actions used

The app uses these Accessibility attributes/actions as part of the calls above:

- `AXWindows`: enumerate an app's windows.
- `AXFocusedWindow`: read or set the focused window.
- `AXMain`: mark a window as the main window.
- `AXTitle`: read a window title.
- `AXPosition`: read a window origin.
- `AXSize`: read a window size.
- `AXFrame`: read an element's full rectangle.
- `AXRaise`: raise a window to the front.
- `AXChildren`: walk the Dock accessibility tree.
- `AXSelectedChildren`: detect the currently selected Space thumbnail in Mission Control.

#### Dock / Mission Control usage

Through the Rust `accessibility` crate, the app also uses public AX concepts such as:

- `AXUIElement::application_with_bundle("com.apple.dock")`
  - Gets the Dock's AX root so SpaceHound can inspect Mission Control and Expose.

- AX child traversal and identifier reads
  - Used to find Dock elements with identifiers like `mc`, `mc.spaces`, and `appexpose`.
  - This is how the app decides whether Mission Control or Expose is open and which Space thumbnail is selected.

#### Mission Control / App Exposé thumbnails

On current macOS the Dock's `mc` / `appexpose` groups are empty stubs (they still
tell the two overlays apart). The `WindowManager` process owns the overlay: an
accessibility tree of `mc.display` / `appexpose.display` groups holding one
`AXButton` per thumbnail (title, `AXFrame`, a private `wid`), which App Exposé
only builds for a single display, and a display-sized on-screen window above the
normal layers per display while an overlay shows (how the app tells that a swipe
down is dismissing it).

The thumbnails themselves come from `CGWindowListCopyWindowInfo`: while an overlay
shows, the window server reports every on-screen app window at its *thumbnail*
bounds, on every display, front to back. WindowManager's own windows (the
highlight frame) and, in App Exposé, other apps' windows are filtered out (see
`lib/mission_control.cpp`; `examples/mc_probe.cpp` inspects both sources).

The overlay activates the thumbnail under the cursor as it closes, so a dismissal
parks the hidden cursor on the highlighted thumbnail first.

### 2. Core Graphics / Quartz Event Services

These APIs handle input interception, synthetic events, display lookup, window enumeration, and cursor movement.

#### Event tap APIs

- `CGEventTapCreate(...)` (via `CGEvent::tap_create`)
  - Installs a low-level HID event tap.
  - SpaceHound listens for:
    - key down events
    - gesture events
    - left mouse down events
    - scroll wheel events

- `CFMachPortCreateRunLoopSource(...)` (wrapped through Core Foundation helpers)
  - Turns the event tap's Mach port into a run-loop source so it can actually receive events.

#### Event creation and posting APIs

- `CGEventSourceCreate(...)`
  - Creates an event source for synthetic input.

- `CGEventCreate(...)`
  - Creates a bare event object, later populated with gesture fields for private swipe synthesis.

- `CGEventCreateMouseEvent(...)`
  - Used to synthesize mouse move/down/up events, including menu-bar clicks on another display.
  - The empty-display click targets a spot that hit-tests as bare menu bar on the target
    display (`AXUIElementCopyElementAtPosition` on the system-wide element, role
    `AXMenuBar`, owned by the same process as the bar's left margin). Each display's bar
    belongs to the app last active there, so the layout is read from that display rather
    than from the frontmost app. The cursor is warped there and back around the click;
    `CGEventSourceCounterForEventType` (HID state, left mouse-up) tells when the click
    has been applied so the return warp cannot be undone by it.

- `CGEventPost(...)`
  - Posts a synthetic event into the system event stream.

- `CGEventTapPostEvent(...)`
  - Posts replacement events through the active event tap proxy.
  - Used when the app swallows an incoming event and injects its own replacement gesture.

- `CGEventCreateSourceFromEvent(...)`
  - Builds a reusable source from an existing incoming event.

#### Event inspection / mutation APIs

- `CGEventGetFlags` / `CGEventSetFlags`
  - Reads and updates modifier flags.

- `CGEventGetLocation`
  - Reads event cursor position.

- `CGEventGetIntegerValueField` / `CGEventSetIntegerValueField`
  - Reads and writes integer fields on events.
  - Public for normal fields, but SpaceHound also uses it with private field IDs for gestures.

- `CGEventGetDoubleValueField` / `CGEventSetDoubleValueField`
  - Same idea for floating-point event fields.

#### Cursor and click APIs

- `CGWarpMouseCursorPosition(...)`
  - Moves the cursor instantly.
  - Used to ensure the cursor is on the correct display before posting gesture events, and to anchor focus when a Space has no windows.
  - Also the basis of the sub-frame "cursor hop" (`cursor_hop` in `display_switch.cpp`): warp to a point, post a HID event there, wait until the window server applied it, warp back. Used for the empty-display click and for hovering Mission Control thumbnails, whose highlight follows the real cursor and stays put after it warps away.

- `CGDisplayHideCursor(...)` / `CGDisplayShowCursor(...)`
  - Hide the cursor for the duration of a thumbnail hover hop, so it is never seen at the thumbnail. From a background process this only works together with the `SetsCursorInBackground` connection property below.

#### Display APIs

- `CGGetActiveDisplayList(...)`
  - Enumerates active displays.

- `CGGetDisplaysWithPoint(...)`
  - Finds the display containing a screen coordinate.

- `CGDisplayBounds(...)`
  - Returns a display's global bounds.

- `CGDisplayCreateUUIDFromDisplayID(...)`
  - Converts a display ID into a stable UUID string.

#### Window-list APIs

- `CGWindowListCopyWindowInfo(...)`
  - Returns the current Window Server window list as dictionaries.
  - Used to:
    - enumerate on-screen windows
    - find the topmost window on a display
    - map between CG window info and AX windows for focusing

The app reads standard `CGWindowListCopyWindowInfo` dictionary keys such as:

- `kCGWindowNumber`
- `kCGWindowOwnerPID`
- `kCGWindowLayer`
- `kCGWindowName`
- `kCGWindowOwnerName`
- `kCGWindowBounds`
- `kCGWindowAlpha`
- `kCGWindowIsOnscreen`

### 3. AppKit APIs

These APIs support Space-change observation, app activation, and tray coordinate scaling.

#### Space-change observation

- `NSWorkspace.sharedWorkspace`
  - Accesses the shared workspace object.

- `NSWorkspace.notificationCenter`
  - Gets the notification center associated with `NSWorkspace`.

- `addObserverForName:object:queue:usingBlock:` with `NSWorkspaceActiveSpaceDidChangeNotification`
  - Registers a callback when the active Space changes.
  - SpaceHound uses this to keep the tray title synced with the current Space number.

#### App activation

- `NSRunningApplication::runningApplicationWithProcessIdentifier(pid)`
  - Resolves an app by PID.

- `NSRunningApplication.isActive`
  - Checks whether that app is currently frontmost.

- `NSRunningApplication.activateWithOptions(...)`
  - Brings the target app forward, using `ActivateIgnoringOtherApps`.
  - Used after AX-based window focus/raise so the app is truly frontmost.

#### Screen scale factor

- `NSScreen.mainScreen`
  - Gets the main screen.

- `NSScreen.backingScaleFactor`
  - Reads the Retina scale factor.
  - Used when converting between AppKit and Core Graphics coordinate systems.

### 4. Core Foundation APIs

Core Foundation is the glue for many of the APIs above.

- `CFRunLoopGetCurrent` / `CFRunLoop::current()`
  - Gets the current run loop.

- `CFRunLoopAddSource(...)`
  - Adds the event tap's run-loop source.

- `CFDictionaryCreate(...)`
  - Used to build the options dictionary for `AXIsProcessTrustedWithOptions`.

- `CFString`, `CFNumber`, `CFArray`, `CFMutableArray`, `CFDictionary`
  - Used heavily to parse system-returned dictionaries and arrays, especially from private Space APIs and `CGWindowListCopyWindowInfo`.

- `CFUUIDCreateString` equivalent (`CFUUID::new_string(...)`)
  - Converts a display UUID object into a string.

### 5. Grand Central Dispatch

- `_dispatch_main_q`
- `dispatch_async_f(...)`

SpaceHound wraps these in `dispatch_to_main(...)` so window-focus restoration can hop back to the main thread. That matters because AppKit-triggering work, especially the AX raise/focus sequence, is safest on the main thread.

## Private / Undocumented APIs Used In The Main App Flow

These are the most maintenance-sensitive parts of the app.

### 1. Private CGS / SkyLight Space APIs

#### `CGSMainConnectionID()`

- Returns the process's Core Graphics Services connection.
- SpaceHound uses it as the entry point for private Space/display queries.

#### `CGSCopyManagedDisplaySpaces(connection)`

- Returns the Window Server's model of displays and their Spaces.
- SpaceHound uses it to:
  - list Spaces per display
  - find the current Space on a display
  - count Spaces for tray updates and navigation logic

#### `CGSCopyActiveMenuBarDisplayIdentifier(connection)`

- Returns the UUID of the display currently hosting the menu bar.
- SpaceHound treats that as the active display for display-cycling and tray state.

### 2. Private `_AXUIElementGetWindow(...)`

- Maps an AX window element back to a `CGWindowID`.
- Used when matching AX windows to the `CGWindowListCopyWindowInfo` window the app wants to focus.
- This improves window matching beyond title/bounds heuristics.

### 3. Undocumented gesture event type and fields

The app synthesizes swipe-like gestures by constructing a `CGEvent` with private field values.

#### Private event type

- `CGEventType(30)`
  - Treated by the app as a gesture event.
  - Used for Space switching, Mission Control open/close, and Expose open/close.

#### Private event fields used

- Field `0x37`
  - Written as an internal event-type marker.

- Field `0x6e`
  - Gesture subtype marker.

- Field `0x7b`
  - Gesture kind: horizontal vs. vertical.

- Field `0x7c`
  - Gesture delta / direction sign.

- Fields `0x81` and `0x82`
  - Larger end-phase deltas for the completed gesture.

- Field `0x84`
  - Gesture phase.

- Field `0x86`
  - Mirror of gesture phase.

- Field `0x8a`
  - Finger-count marker, set to a three-finger gesture value.

- Field `0xa5`
  - Secondary gesture kind marker.

- Field `123`
  - Used in two places:
    - to interpret gesture direction/type on incoming gesture events
    - to read scroll momentum phase when filtering tray scroll behavior

In practice, this undocumented event shape is what lets SpaceHound replace normal system swipes with faster or more controlled synthetic Space/Mission Control gestures.

## Runtime Behavior Tied To These APIs

### Switching Spaces

SpaceHound does not call a public "switch to Space N" API, because macOS does not expose one. Instead it:

1. Reads Space state with `CGSCopyManagedDisplaySpaces`.
2. Determines the target Space in app logic.
3. Ensures the cursor is on the target display with `CGWarpMouseCursorPosition`.
4. Synthesizes a private gesture event sequence (`Begin`, `Update`, `End`) using undocumented `CGEvent` fields.

### Detecting Mission Control and Expose state

The app checks the Dock's AX hierarchy to determine whether:

- Mission Control is open
- Expose is open
- a specific Mission Control Space thumbnail is selected

That is done through AX child enumeration plus attributes like `AXChildren`, `AXSelectedChildren`, and `AXFrame`.

### Focusing windows across displays

To move focus to another display, the app:

1. Enumerates windows with `CGWindowListCopyWindowInfo`.
2. Chooses a target window based on display bounds.
3. Creates an AX application element for that window's owning PID.
4. Finds the matching AX window, using `_AXUIElementGetWindow` when possible.
5. Raises and focuses it with `AXRaise`, `AXFocusedWindow`, and `AXMain`.
6. Activates the owning app with `NSRunningApplication.activateWithOptions(...)`.

### Keeping the tray title in sync

The tray title shows the current Space number. It is updated by:

- listening for `NSWorkspaceActiveSpaceDidChangeNotification`
- reading current display/Space state with private CGS APIs
- converting tray icon geometry with `NSScreen.backingScaleFactor`

## Permissions And Platform Constraints

The API set above implies some important runtime constraints:

- Accessibility permission is required.
  - Needed for AX reads/writes and for event injection behavior that interacts with other apps.

- The app is intentionally not sandboxed.
  - The entitlements files explicitly disable the app sandbox because the private CGS APIs are incompatible with a normal sandboxed setup.

- The build links against private frameworks.
  - Both build scripts add `/System/Library/PrivateFrameworks` to the framework search path.

- The app opens System Settings with an `x-apple.systempreferences:` URL.
  - This is how it deep-links the user to the Accessibility privacy pane.

## Additional Wrapped APIs Present In The Codebase But Not On The Normal Runtime Path

These exist in `~/work/spacehound`, but I did not find them used from the main startup/hotkey/tray flow:

- `CGSCopySpacesForWindows(...)`
  - Wrapped for reading which Spaces a window belongs to.

- `CGSAddWindowsToSpaces(...)`
  - Wrapped for moving a window into a Space.

- `CGSRemoveWindowsFromSpaces(...)`
  - Wrapped for removing a window from its current Spaces before reassigning it.

- `SLSSetActiveMenuBarDisplayIdentifier(...)`
  - Wrapped for setting the active menu-bar display directly. The window server ignores
    it from third-party processes, so it is not used.
- `_SLPSSetFrontProcessWithOptions(...)` / `SLPSPostEventRecordTo(...)`
  - Used by `focus_window` to bring a specific window front without the LaunchServices
    activation round-trip (the same sequence tiling window managers use).
- `CGSSetConnectionProperty(...)`
  - Used to set `SetsCursorInBackground` on our own connection so that
    `CGDisplayHideCursor` takes effect while the app is not frontmost.

- AX observer APIs such as:
  - `AXObserverCreate(...)`
  - `AXObserverAddNotification(...)`
  - `AXObserverGetRunLoopSource(...)`

- `CGEventTapEnable(...)`
  - Wrapped on the event-tap type, but not called by the current app flow.

- `CGEventCreateKeyboardEvent(...)`
  - Wrapped in the event layer, but not used by the current app flow.

- `CFRunLoopRun()`
  - Wrapped in the run-loop helper, but not used directly by the current in-process app runtime.

Those wrappers suggest the codebase has experimented with deeper native integration, but the app's current flow mainly relies on:

- AX window access
- CG event taps and synthetic events
- AppKit Space-change notifications
- private CGS Space queries

## Bottom Line

SpaceHound is fundamentally a macOS-native control app built around:

- Accessibility for reading and manipulating UI state
- Core Graphics for event interception and synthetic input
- AppKit for app/display integration and Space-change notifications
- private CGS/SkyLight behavior for Spaces, which is the key unsupported dependency

If you are evaluating maintenance risk, the highest-risk pieces are:

1. Private CGS/SkyLight calls
2. Undocumented `CGEvent` gesture fields
3. `_AXUIElementGetWindow`

Those are the pieces most likely to break across future macOS releases.
