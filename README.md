<p align="center">
  <img height="150" src="https://github.com/animaslabs/spacerabbit/blob/main/logo.png">
</p>

<h1 align="center">SpaceRabbit</h1>

<p align="center">Fast workspace navigation for macOS.</p>

---

SpaceRabbit lives in your menu bar and makes moving between macOS Spaces (desktops)
and displays instant. Jump straight to a numbered Space, step left or right, move
focus between windows, or trigger Mission Control — all from the keyboard, without
waiting on the built-in animations. The menu bar always shows which Space you're on.

## Install

1. Download the [**latest SpaceRabbit DMG**](https://updates.getspacerabbit.com/releases/latest/SpaceRabbit-arm64.dmg).
2. Open the DMG and drag **SpaceRabbit** to your Applications folder.
3. Launch SpaceRabbit. It appears in the menu bar — there is no Dock icon or main
   window.

The same release artifacts are mirrored on the
[GitHub Releases page](https://github.com/animaslabs/spacerabbit/releases/latest).

**Requirements:** macOS 14 (Sonoma) or newer.

### Grant Accessibility access

SpaceRabbit needs macOS **Accessibility** access to switch Spaces and manage
windows. On first launch it will ask, and offer to open **System Settings ›
Privacy & Security › Accessibility** for you — turn on the toggle next to
SpaceRabbit.

You don't need to relaunch: SpaceRabbit starts working automatically the moment
access is granted. If you skip the prompt, the menu bar shows a ⚠️ badge and a
**Grant Accessibility Access…** item you can use later.

## Features

- **Instant Space switching** — jump to any of your first 10 Spaces by number, or
  step left/right, with a keystroke.
- **Display switching** — move to a specific display or step between displays.
- **Window focus** — cycle focus to the next or previous window.
- **System shortcuts** — toggle Mission Control and App Exposé.
- **Menu bar indicator** — always shows the number of the Space you're currently
  on.
- **Scroll to switch** — scroll over the menu bar icon to move between Spaces.
- **Fully customizable hotkeys** — rebind or disable any shortcut in Settings.
- **Wrap-around navigation** — optionally loop from the last Space back to the
  first (and the same for displays).

## The menu bar

The menu bar shows the current Space number (for example, **3**). Click it for the
menu:

- **Space _X_ of _Y_** — the current status (or a message if access is needed).
- **Grant Accessibility Access…** — shown only when access hasn't been granted yet.
- **Settings…** (⌘,) — open the Settings window.
- **Receive Beta Updates** — opt into or out of signed release candidates.
- **Check for Updates…** — check the signed SpaceRabbit update feed immediately.
- **Quit SpaceRabbit** (⌘Q).

## Automatic updates

SpaceRabbit checks its signed update feed once per day. When a new version is
available, Sparkle shows the release notes and lets you install it. Automatic
installation is opt-in; you can enable it from Sparkle's update prompt.

Every update is Developer ID signed, notarized by Apple, and independently
signed with SpaceRabbit's Sparkle Ed25519 key. If the feed, archive, or signature
cannot be verified, the installed app is left unchanged.

## Crash reporting

Distributed builds automatically send crash reports to Sentry so failures can
be diagnosed. Reports contain the crash signal or exception, native stack
trace, SpaceRabbit version/build, and basic macOS/device diagnostics. SpaceRabbit
does not send its settings file, user identity, screenshots, logs, analytics,
performance traces, or network activity to Sentry.

## Keyboard shortcuts

Every shortcut below is a default and can be changed or turned off in Settings.
The default modifier is **⌥ Option** (with **⌃ Control** added for display
actions).

### Workspace (Spaces)

| Action | Default shortcut |
| --- | --- |
| Switch Space Left | ⌥A |
| Switch Space Right | ⌥D |
| Switch to Space 1–9 | ⌥1 … ⌥9 |
| Switch to Space 10 | ⌥0 |

### Display

| Action | Default shortcut |
| --- | --- |
| Switch Display Left | ⌥⌃A |
| Switch Display Right | ⌥⌃D |
| Switch to Display 1–9 | ⌥⌃1 … ⌥⌃9 |
| Switch to Display 10 | ⌥⌃0 |

### Window focus

| Action | Default shortcut |
| --- | --- |
| Focus Next Window | ⌥Tab |
| Focus Previous Window | ⌥⇧Tab |

### System

| Action | Default shortcut |
| --- | --- |
| Toggle Mission Control | ⌥W |
| Toggle App Exposé | ⌥E |

## Settings

Open **Settings…** from the menu bar (or press ⌘, while SpaceRabbit is active).

### Hotkeys

Each action has its own row. Click the shortcut field to record a new key
combination — press the keys you want, or press **Escape** to cancel. Use the ✕
button to clear a shortcut, or the toggle on the left to turn an action off
without losing its binding.

### General

| Setting | What it does | Default |
| --- | --- | --- |
| **Launch at login** | Automatically open SpaceRabbit when you sign in. | Off |
| **Wrap workspace navigation** | Loop back to the first Space after the last (and vice-versa). | Off |
| **Wrap display navigation** | Loop across the left and right display edges. | Off |
| **Enable tray scroll switching** | Scroll over the menu bar icon to change Spaces. | On |
| **Invert tray scroll direction** | Reverse the scroll direction for switching. | Off |
| **Enable fast swipe** | Trigger swipe actions with a lighter, quicker gesture. | On |

Changes take effect when you press **Save**. **Reload** discards unsaved edits and
re-reads the file from disk, and **Reveal in Finder** opens the settings file's
location.

The **Launch at login** setting is managed by macOS and does not live in
`settings.json`. If macOS requires approval, SpaceRabbit offers to open **System
Settings › General › Login Items**.

## Where settings are stored

Your configuration lives in a plain JSON file:

```
~/Library/Application Support/SpaceRabbit/settings.json
```

SpaceRabbit creates it on first launch. You can edit it by hand if you prefer —
use **Reveal in Finder** in Settings to find it.

## Uninstall

1. Quit SpaceRabbit from the menu bar.
2. Move **SpaceRabbit** from Applications to the Trash.
3. Optionally remove your settings:
   `~/Library/Application Support/SpaceRabbit/`.
4. Remove SpaceRabbit from **System Settings › Privacy & Security ›
   Accessibility** if you like.

## Development

Building from source and architecture notes are documented in
[DEVELOPMENT.md](DEVELOPMENT.md). Maintainers should use
[RELEASING.md](RELEASING.md) when publishing a release.
