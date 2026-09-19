# SpaceHound Settings JSON Schema

## Purpose

This document defines the canonical shared `settings.json` contract for SpaceHound.

It should be treated as the target schema for:

- `~/work/spacehound` as the GUI/editor and primary writer
- `~/work/spacehound-core` as the daemon/runtime reader

## Naming Convention

The canonical config format uses `camelCase` for config fields:

- top-level fields use `camelCase`
- nested object fields use `camelCase`
- hotkey action ids remain stable string identifiers and currently use `snake_case`

This keeps the JSON config aligned with common JSON conventions and with the current app structure, while avoiding a disruptive rename of the existing hotkey action ids.

## Current Implementation Note

The current AppKit menu bar app writes this file through `SHSettingsStore`, and
the in-process C++ runtime reads it through `spacehound::settings`. Both sides
use `camelCase` for the top-level fields documented here.

## File Location

The settings file path remains:

- `~/Library/Application Support/SpaceHound/settings.json`

The runtime also keeps compatibility with older XDG-style config locations when
it is launched outside the app.

## Format Summary

- encoding: UTF-8 JSON object
- current schema version string: `"1.0"`
- canonical field style: `camelCase`

## Top-Level Shape

```json
{
  "version": "1.0",
  "workspaceWrap": false,
  "displayWrap": false,
  "trayScroll": true,
  "trayScrollInverted": false,
  "hotkeys": {
    "switch_space_left": {
      "key": "a",
      "modifiers": ["option"],
      "enabled": true
    }
  },
  "fastSwipe": true,
  "moveCursorToActiveDisplay": true,
  "moveCursorToTargetDisplay": true
}
```

## Field Definitions

### `version`

- type: string
- required: yes
- current value: `"1.0"`

Writers should always emit the current schema version. Readers should reject unsupported major versions and may apply compatibility handling for older known versions.

### `workspaceWrap`

- type: boolean
- required: yes
- default when generating a fresh file: `false`

Controls whether workspace navigation wraps from first to last or last to first.

### `displayWrap`

- type: boolean
- required: yes
- default when generating a fresh file: `false`

Controls whether display navigation wraps across the left/right edge.

### `trayScroll`

- type: boolean
- required: yes
- default when generating a fresh file: `true`

Controls whether scrolling over the tray icon switches workspaces.

### `trayScrollInverted`

- type: boolean
- required: yes
- default when generating a fresh file: `false`

Controls tray-scroll direction inversion.

### `hotkeys`

- type: object
- required: yes
- value type: `hotkeySetting | null`

Maps stable action ids to either:

- a hotkey binding object
- `null`, meaning the action is unbound/disabled

Writers should emit the complete set of known action ids. Readers should tolerate unknown ids and missing ids.

### `fastSwipe`

- type: boolean
- required: yes
- default when generating a fresh file: `true`

Controls fast gesture activation for swipe-based actions.

### `moveCursorToActiveDisplay`

- type: boolean
- required: no (readers default to `true` when absent)
- default when generating a fresh file: `true`

Controls which display a workspace switch (`switch_space_*`) targets when
displays have separate Spaces. When `true`, the cursor is moved onto the display
that owns the focused window before the switch, so that display changes Space.
When `false`, the cursor is left alone and the Space changes on whichever display
the cursor is currently on; wrapping and numbered targets are computed against
that display. The setting has no effect when displays share a single set of
Spaces.

### `moveCursorToTargetDisplay`

- type: boolean
- required: no (readers default to `true` when absent)
- default when generating a fresh file: `true`

Controls whether a display switch (`switch_display_*`) moves the cursor onto the
destination display. When `true`, the cursor is warped to the top centre of the
target display before it is activated. When `false`, the cursor is left where it
is and the target display is activated by focusing its frontmost window. If the
target display has no windows, the app activates it by briefly owning an
invisible key window there; the cursor is not moved.

## `hotkeySetting`

```json
{
  "key": "a",
  "modifiers": ["option"],
  "enabled": true
}
```

### Fields

#### `key`

- type: string
- required: yes

Accepted values in the current implementation:

- single letters: `"a"` through `"z"` or uppercase equivalents
- single digits: `"0"` through `"9"`
- special keys:
  - `"escape"` or `"esc"`
  - `"return"` or `"enter"`
  - `"space"`
  - `"tab"`
  - `"delete"` or `"backspace"`
  - `"left"` or `"arrowleft"`
  - `"right"` or `"arrowright"`
  - `"up"` or `"arrowup"`
  - `"down"` or `"arrowdown"`
  - `"["` or `"leftbracket"`
  - `"]"` or `"rightbracket"`
  - `"-"` or `"minus"`
  - `"="` or `"equal"`

For compatibility, writers should prefer the canonical forms already used in defaults:

- lowercase letters and digits
- `"Tab"` for tab

#### `modifiers`

- type: array of strings
- required: yes
- items may appear in any order

Accepted modifier strings in the current implementation:

- `"ctrl"`
- `"option"`
- `"alt"`
- `"cmd"`
- `"command"`
- `"meta"`
- `"shift"`

Recommended canonical output values:

- `"ctrl"`
- `"option"`
- `"cmd"`
- `"shift"`

#### `enabled`

- type: boolean
- required: yes

If `false`, the binding must be treated as disabled even if `key` and `modifiers` are present.

## Known Hotkey Action IDs

These are the stable v1 action ids.

### Workspace navigation

- `switch_space_left`
- `switch_space_right`
- `switch_space_1`
- `switch_space_2`
- `switch_space_3`
- `switch_space_4`
- `switch_space_5`
- `switch_space_6`
- `switch_space_7`
- `switch_space_8`
- `switch_space_9`
- `switch_space_10`

### Display navigation

- `switch_display_left`
- `switch_display_right`
- `switch_display_1`
- `switch_display_2`
- `switch_display_3`
- `switch_display_4`
- `switch_display_5`
- `switch_display_6`
- `switch_display_7`
- `switch_display_8`
- `switch_display_9`
- `switch_display_10`

### Window focus

- `window_focus_next`
- `window_focus_prev`

### System actions

- `mission_control_toggle`
- `expose_toggle`

## Default Values

The canonical default document is:

```json
{
  "version": "1.0",
  "workspaceWrap": false,
  "displayWrap": false,
  "trayScroll": true,
  "trayScrollInverted": false,
  "hotkeys": {
    "switch_space_left": { "key": "a", "modifiers": ["option"], "enabled": true },
    "switch_space_right": { "key": "d", "modifiers": ["option"], "enabled": true },
    "switch_space_1": { "key": "1", "modifiers": ["option"], "enabled": true },
    "switch_space_2": { "key": "2", "modifiers": ["option"], "enabled": true },
    "switch_space_3": { "key": "3", "modifiers": ["option"], "enabled": true },
    "switch_space_4": { "key": "4", "modifiers": ["option"], "enabled": true },
    "switch_space_5": { "key": "5", "modifiers": ["option"], "enabled": true },
    "switch_space_6": { "key": "6", "modifiers": ["option"], "enabled": true },
    "switch_space_7": { "key": "7", "modifiers": ["option"], "enabled": true },
    "switch_space_8": { "key": "8", "modifiers": ["option"], "enabled": true },
    "switch_space_9": { "key": "9", "modifiers": ["option"], "enabled": true },
    "switch_space_10": { "key": "0", "modifiers": ["option"], "enabled": true },
    "switch_display_left": { "key": "a", "modifiers": ["option", "ctrl"], "enabled": true },
    "switch_display_right": { "key": "d", "modifiers": ["option", "ctrl"], "enabled": true },
    "switch_display_1": { "key": "1", "modifiers": ["option", "ctrl"], "enabled": true },
    "switch_display_2": { "key": "2", "modifiers": ["option", "ctrl"], "enabled": true },
    "switch_display_3": { "key": "3", "modifiers": ["option", "ctrl"], "enabled": true },
    "switch_display_4": { "key": "4", "modifiers": ["option", "ctrl"], "enabled": true },
    "switch_display_5": { "key": "5", "modifiers": ["option", "ctrl"], "enabled": true },
    "switch_display_6": { "key": "6", "modifiers": ["option", "ctrl"], "enabled": true },
    "switch_display_7": { "key": "7", "modifiers": ["option", "ctrl"], "enabled": true },
    "switch_display_8": { "key": "8", "modifiers": ["option", "ctrl"], "enabled": true },
    "switch_display_9": { "key": "9", "modifiers": ["option", "ctrl"], "enabled": true },
    "switch_display_10": { "key": "0", "modifiers": ["option", "ctrl"], "enabled": true },
    "window_focus_next": { "key": "Tab", "modifiers": ["option"], "enabled": true },
    "window_focus_prev": { "key": "Tab", "modifiers": ["option", "shift"], "enabled": true },
    "mission_control_toggle": { "key": "w", "modifiers": ["option"], "enabled": true },
    "expose_toggle": { "key": "e", "modifiers": ["option"], "enabled": true }
  },
  "fastSwipe": true,
  "moveCursorToActiveDisplay": true,
  "moveCursorToTargetDisplay": true
}
```

## Disabled Action Semantics

An action may be disabled in either of these forms:

### `null`

```json
{
  "hotkeys": {
    "mission_control_toggle": null
  }
}
```

### `enabled: false`

```json
{
  "hotkeys": {
    "mission_control_toggle": {
      "key": "w",
      "modifiers": ["option"],
      "enabled": false
    }
  }
}
```

Both should be treated as disabled by readers. Writers should prefer one representation consistently. The simpler representation is `null`.

## Compatibility Rules

### Recommended reader behavior

Readers should behave like this:

- unknown top-level fields are ignored
- the retired `telemetryEnabled` top-level field is ignored for compatibility
  with existing settings files and should be removed by writers
- unknown fields inside hotkey objects are ignored
- unknown hotkey action ids are preserved or ignored safely
- missing known hotkey action ids are backfilled with defaults where appropriate
- one invalid hotkey binding should not invalidate the entire file if the rest of the config is usable

## Normative Validation Rules

Readers should enforce at least these rules:

- top-level value must be an object
- `version` must be a string
- `workspaceWrap`, `displayWrap`, `trayScroll`, `trayScrollInverted`, `fastSwipe`, `moveCursorToActiveDisplay`, and `moveCursorToTargetDisplay` must be booleans
- `hotkeys` must be an object
- each `hotkeys` value must be either `null` or a valid `hotkeySetting`
- `hotkeySetting.key` must be a non-empty string
- `hotkeySetting.modifiers` must be an array of strings
- `hotkeySetting.enabled` must be a boolean

Readers may be stricter for runtime use by rejecting unsupported key names or modifier names.

## JSON Schema Draft

This is a practical draft schema for validation tooling. It encodes the canonical `camelCase` form.

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://spacehound.app/schemas/settings-1.0.json",
  "title": "SpaceHound Settings",
  "type": "object",
  "required": [
    "version",
    "workspaceWrap",
    "displayWrap",
    "trayScroll",
    "trayScrollInverted",
    "hotkeys",
    "fastSwipe"
  ],
  "properties": {
    "version": {
      "type": "string",
      "const": "1.0"
    },
    "workspaceWrap": { "type": "boolean" },
    "displayWrap": { "type": "boolean" },
    "trayScroll": { "type": "boolean" },
    "trayScrollInverted": { "type": "boolean" },
    "fastSwipe": { "type": "boolean" },
    "moveCursorToActiveDisplay": { "type": "boolean" },
    "moveCursorToTargetDisplay": { "type": "boolean" },
    "hotkeys": {
      "type": "object",
      "additionalProperties": {
        "anyOf": [
          { "type": "null" },
          {
            "type": "object",
            "required": ["key", "modifiers", "enabled"],
            "properties": {
              "key": { "type": "string", "minLength": 1 },
              "modifiers": {
                "type": "array",
                "items": { "type": "string" }
              },
              "enabled": { "type": "boolean" }
            },
            "additionalProperties": true
          }
        ]
      }
    }
  },
  "additionalProperties": true
}
```

## Recommendations For Writers

- always emit the complete document, not sparse patches
- emit canonical `camelCase` field names
- prefer canonical modifier names: `ctrl`, `option`, `cmd`, `shift`
- prefer `null` instead of `enabled: false` if you want a single canonical disabled representation
- write atomically using temp-file plus rename

## Recommendations For Readers

- tolerate unknown fields unless the schema version changes incompatibly
- tolerate unknown hotkey action ids so forward-compatible config files do not break older builds
- log invalid bindings clearly and continue when possible rather than failing the whole config for one bad hotkey
