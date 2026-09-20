<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="logo.png">
    <img height="210" alt="SpaceHound logo" src="logo-black.png">
  </picture>
</p>

<h1 align="center">SpaceHound</h1>

<p align="center">Fast workspace navigation for macOS.</p>


SpaceHound lives in your menu bar and makes moving between macOS Spaces (desktops)
and displays instant. Jump straight to a numbered Space, step left or right, move
focus between windows, or trigger Mission Control — all from the keyboard, without
waiting on the built-in animations. The menu bar always shows which Space you're on.

## Install

1. Download the DMG from the [**latest release**](https://github.com/jjkr/spacehound/releases/latest).
2. Open the DMG and drag **SpaceHound** to your Applications folder.
3. Launch SpaceHound. It appears in the menu bar — there is no Dock icon or main
   window.

Every version, including beta pre-releases, is on the
[GitHub Releases page](https://github.com/jjkr/spacehound/releases).

**Requirements:** macOS 14 (Sonoma) or newer.

### Grant Accessibility access

SpaceHound needs macOS **Accessibility** access to switch Spaces and manage
windows. On first launch it will ask, and offer to open **System Settings ›
Privacy & Security › Accessibility** for you — turn on the toggle next to
SpaceHound.

You don't need to relaunch: SpaceHound starts working automatically the moment
access is granted. If you skip the prompt, the menu bar shows a ⚠️ badge and a
**Grant Accessibility Access…** item you can use later.

## Features

- **Instant Space switching** — jump to any of your first 10 Spaces by number, or
  step left/right, with a keystroke.
- **Display switching** — move to a specific display or step between displays.
- **Window focus** — cycle focus to the next or previous window. While Mission
  Control or App Exposé is showing, the same keys move the hover highlight
  between window thumbnails instead, and opening either with a SpaceHound
  hotkey highlights the focused window right away.
- **System shortcuts** — toggle Mission Control and App Exposé.
- **Menu bar indicator** — always shows the number of the Space you're currently
  on.
- **Scroll to switch** — scroll over the menu bar icon to move between Spaces.
- **Fully customizable hotkeys** — rebind or disable any shortcut in Settings.
- **Wrap-around navigation** — optionally loop from the last Space back to the
  first (and the same for displays).

## The menu bar

The menu bar shows the current Space number (for example, **3**). If SpaceHound
can't run, it shows a ⚠️ badge instead and the menu explains why. Click it for the
menu:

- **Grant Accessibility Access…** — shown only when access hasn't been granted yet.
- **Settings…** (⌘,) — open the Settings window.
- **Check for Updates…** — check the signed SpaceHound update feed immediately.
- **Quit SpaceHound** (⌘Q).

## Automatic updates

SpaceHound checks its signed update feed once per day. When a new version is
available, Sparkle shows the release notes and lets you install it. Automatic
installation is opt-in; you can enable it from Sparkle's update prompt.

Every update is Developer ID signed, notarized by Apple, and independently
signed with SpaceHound's Sparkle Ed25519 key. If the feed, archive, or signature
cannot be verified, the installed app is left unchanged.

## Crash reporting

Crash reporting is opt-in. On first launch SpaceHound asks whether it may send
crash reports to Sentry, and the choice can be changed at any time with
**Send crash reports** in Settings. Nothing is sent until you opt in.

When enabled, reports contain the crash signal or exception, native stack
trace, SpaceHound version/build, and basic macOS/device diagnostics. SpaceHound
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

With Mission Control or App Exposé open, these step the highlight through the
visible thumbnails in reading order (left to right, top to bottom). When
SpaceHound opened the overlay (toggle shortcut or fast swipe), the focused
window is highlighted as soon as it appears and the keys step on from there
(swiping it open highlights nothing, since the mouse is in play then);
otherwise the first press lands on the focused window, or steps away from the
thumbnail under the cursor. Dismissing the overlay through SpaceHound brings
the highlighted window to the front. The display shortcuts move the highlight
to the frontmost thumbnail on the target display (in App Exposé, skipping
displays the app has no windows on), and inside Mission Control
the Space shortcuts highlight the frontmost window of the Space they switch
to (in App Exposé, left/right switch apps as they do natively).

### System

| Action | Default shortcut |
| --- | --- |
| Toggle Mission Control | ⌥W |
| Toggle App Exposé | ⌥E |

## Settings

Open **Settings…** from the menu bar (or press ⌘, while SpaceHound is active).

### Hotkeys

Each action has its own row. Click the shortcut field to record a new key
combination — press the keys you want, or press **Escape** to cancel. Use the ✕
button to clear a shortcut, or the toggle on the left to turn an action off
without losing its binding.

### General

| Setting | What it does | Default |
| --- | --- | --- |
| **Launch at login** | Automatically open SpaceHound when you sign in. | Off |
| **Wrap workspace navigation** | Loop back to the first Space after the last (and vice-versa). | Off |
| **Wrap display navigation** | Loop across the left and right display edges. | Off |
| **Enable tray scroll switching** | Scroll over the menu bar icon to change Spaces. | On |
| **Invert tray scroll direction** | Reverse the scroll direction for switching. | Off |
| **Enable fast swipe** | Trigger swipe actions with a lighter, quicker gesture. | On |

### Advanced

| Setting | What it does | Default |
| --- | --- | --- |
| **Receive beta updates** | Opt into beta releases, which ship before production releases and are signed and notarized the same way. | Off |
| **Send crash reports** | Send a report to Sentry when SpaceHound crashes. See [Crash reporting](#crash-reporting). | Off |
| **Switch workspaces on the focused display** | Change the Space on the display with the focused window (the cursor is not moved). When off, the Space changes on the display under the cursor. | On |
| **Move cursor to the target display** | Jump the cursor to the destination display when switching displays. When off, the cursor stays where it is. | On |

Changes take effect when you press **OK** (which also closes the window) or
**Apply** (which keeps it open). While there are edits you haven't saved, the
window shows **Unsaved changes** in its lower-left corner and asks whether to
save them before it closes, reloads, or the app quits. **Reload** discards
unsaved edits and re-reads the file from disk, and **Reveal in Finder** opens
the settings file's location.

The **Launch at login** setting is managed by macOS and does not live in
`settings.json`. If macOS requires approval, SpaceHound offers to open **System
Settings › General › Login Items**. **Receive beta updates** is likewise stored
outside `settings.json`, in the app's preferences.

## Where settings are stored

Your configuration lives in a plain JSON file:

```
~/Library/Application Support/SpaceHound/settings.json
```

SpaceHound creates it on first launch. You can edit it by hand if you prefer —
use **Reveal in Finder** in Settings to find it.

## Uninstall

1. Quit SpaceHound from the menu bar.
2. Move **SpaceHound** from Applications to the Trash.
3. Optionally remove your settings:
   `~/Library/Application Support/SpaceHound/`.
4. Remove SpaceHound from **System Settings › Privacy & Security ›
   Accessibility** if you like.

## Development

Building from source and architecture notes are documented in
[DEVELOPMENT.md](DEVELOPMENT.md). Maintainers should use
[RELEASING.md](RELEASING.md) when publishing a release.

## License

SpaceHound is licensed under the [Apache License, Version 2.0](LICENSE). See
[NOTICE](NOTICE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for
attribution. "SpaceHound" and the SpaceHound logo are trademarks of Joe Kramer;
[TRADEMARKS.md](TRADEMARKS.md) explains what that means for forks and
redistribution.
