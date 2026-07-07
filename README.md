<p align="center">
  <img height="150" src="https://github.com/animaslabs/spacerabbit/blob/main/logo.png">
</p>

<h1 align="center">SpaceRabbit</h1>

<p align="center">Fast workspace navigation for macOS.</p>

## Install
Get the latest installer [HERE](https://github.com/animaslabs/spacerabbit/releases/latest).

## Features

- Fast space switching with hotkeys
- Menu bar icon with current workspace number
- More

## Development

This repo contains the macOS menu bar app for SpaceRabbit. It now links the `spacerabbit-core`
runtime directly into the main app process, so there is no separately launched `spacerabbitd`
helper during normal app runs.

The app build uses the repo-local `spacerabbit-core/` sources directly. The app target compiles the
core sources into the main app process, generates `spacerabbit/version.hpp` during the build, and
uses a vendored `nlohmann/json.hpp` header from this repo.

### Prerequisites

- Xcode 26.4 or newer
- [XcodeGen](https://github.com/yonaskolb/XcodeGen) 2.45.3 or newer

### Generate The Project

```sh
make generate
```

### Build The App

```sh
make build
```

### Run The App

```sh
make run
```

The app launches as a menu bar item and can be exited from `Quit SpaceRabbit`.

On first launch the app creates `~/Library/Application Support/SpaceRabbit/settings.json` if it
does not already exist, then starts the SpaceRabbit runtime in-process with that settings path.

## Distribution

For direct GitHub Releases distribution, ship a signed and notarized DMG as the primary download.
The repo now includes:

- `scripts/package-release.sh` to archive an arm64-only release build, sign it with Developer ID,
  notarize a ZIP of the app, staple the app, build a DMG, then notarize and staple the DMG.
- `.github/workflows/release.yml` to run the same flow on GitHub Actions and attach the DMG, ZIP,
  and SHA-256 checksums to a release tag.

### Release Secrets

Set these repository secrets for GitHub Actions:

- `BUILD_CERTIFICATE_BASE64`: base64-encoded Developer ID Application `.p12`
- `P12_PASSWORD`: password for the `.p12`
- `BUILD_KEYCHAIN_PASSWORD`: temporary keychain password used during the job
- `DEVELOPMENT_TEAM`: your Apple Developer Team ID
- `APPLE_API_KEY_BASE64`: base64-encoded App Store Connect API key `.p8`
- `APPLE_API_KEY_ID`: App Store Connect key ID
- `APPLE_API_ISSUER_ID`: App Store Connect issuer ID for team keys

The release app is packaged as a single executable bundle. There is no nested daemon helper to copy
or sign separately.

### Local Signed Build

```sh
export DEVELOPMENT_TEAM=YOURTEAMID
export CODE_SIGN_IDENTITY="Developer ID Application"
export APPLE_API_KEY_PATH=/absolute/path/to/AuthKey_XXXXXX.p8
export APPLE_API_KEY_ID=XXXXXX
export APPLE_API_ISSUER_ID=xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
make package-release
```
