# Development

This document covers building SpaceRabbit from source, its architecture, and how
releases are packaged. For end-user documentation, see the [README](README.md).

## Architecture

SpaceRabbit is a macOS menu bar app (AppKit, Objective-C/Objective-C++) that links
the `spacerabbit-core` runtime directly into the main app process. There is no
separately launched `spacerabbitd` helper during normal app runs.

The app build uses the repo-local `spacerabbit-core/` sources directly. The app
target compiles the core sources into the main app process, generates
`spacerabbit/version.hpp` during the build, and uses a vendored
`nlohmann/json.hpp` header from this repo.

Key source files:

- `SpaceRabbitApp/Sources/AppDelegate.m` — menu bar item, accessibility
  permission workflow, app lifecycle.
- `SpaceRabbitApp/Sources/SRRuntimeHost.mm` — hosts the in-process
  `spacerabbit-core` runtime and drives the menu bar title.
- `SpaceRabbitApp/Sources/SRSettingsWindowController.mm` — the Settings window UI
  (toggles and shortcut recorders).
- `SpaceRabbitApp/Sources/SRSettingsStore.mm` — reads/writes `settings.json` and
  defines the canonical list of hotkey actions.
- `SpaceRabbitApp/Sources/SRPermissions.m` — Accessibility access checks.
- `spacerabbit-core/` — the C++ runtime that performs Space/display/window
  switching. See `spacerabbit-core/docs/` for the settings schema and API notes.

## Prerequisites

- macOS 14.0 or newer (deployment target)
- Xcode 26.4 or newer
- [XcodeGen](https://github.com/yonaskolb/XcodeGen) 2.45.3 or newer

## Generate the project

```sh
make generate
```

## Build the app

```sh
make build
```

## Run the app

```sh
make run
```

The app launches as a menu bar item and can be exited from `Quit SpaceRabbit`.

On first launch the app creates
`~/Library/Application Support/SpaceRabbit/settings.json` if it does not already
exist, then starts the SpaceRabbit runtime in-process with that settings path.

## Settings schema

The canonical `settings.json` contract shared between the app (writer) and the
runtime (reader) is documented in
[`spacerabbit-core/docs/settings-json-schema.md`](spacerabbit-core/docs/settings-json-schema.md).

## Distribution

For direct GitHub Releases distribution, ship a signed and notarized DMG as the
primary download. The repo includes:

- `scripts/package-release.sh` to archive an arm64-only release build, sign it
  with Developer ID, notarize a ZIP of the app, staple the app, build a DMG, then
  notarize and staple the DMG.
- `.github/workflows/release.yml` to run the same flow on GitHub Actions and
  attach the DMG, ZIP, and SHA-256 checksums to a release tag.

The release app is packaged as a single executable bundle. There is no nested
daemon helper to copy or sign separately.

### Release secrets

Set these repository secrets for GitHub Actions:

- `BUILD_CERTIFICATE_BASE64`: base64-encoded Developer ID Application `.p12`
- `P12_PASSWORD`: password for the `.p12`
- `BUILD_KEYCHAIN_PASSWORD`: temporary keychain password used during the job
- `DEVELOPMENT_TEAM`: your Apple Developer Team ID
- `APPLE_API_KEY_BASE64`: base64-encoded App Store Connect API key `.p8`
- `APPLE_API_KEY_ID`: App Store Connect key ID
- `APPLE_API_ISSUER_ID`: App Store Connect issuer ID for team keys

### Local signed build

```sh
export DEVELOPMENT_TEAM=YOURTEAMID
export CODE_SIGN_IDENTITY="Developer ID Application"
export APPLE_API_KEY_PATH=/absolute/path/to/AuthKey_XXXXXX.p8
export APPLE_API_KEY_ID=XXXXXX
export APPLE_API_ISSUER_ID=xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
make package-release
```
