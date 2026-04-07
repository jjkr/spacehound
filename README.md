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

This repo contains the macOS menu bar app shell for SpaceRabbit. The workspace daemon can come from
either:

- a repo-local `spacerabbit-core/` checkout or git submodule
- a sibling `../spacerabbit-core` checkout
- an explicit `SPACERABBITD_PATH`

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

The app launches as a menu bar item, supervises `spacerabbitd`, and can be exited from `Quit SpaceRabbit`.

### Daemon Development

The app bundles `spacerabbitd` into `SpaceRabbit.app/Contents/Helpers/spacerabbitd` at build
time.

By default the Xcode build looks for a local daemon binary at:

- `./spacerabbit-core/build/ninja-release/spacerabbitd`
- `./spacerabbit-core/build/ninja-release/package-shared/bin/spacerabbitd`
- `./spacerabbit-core/build/ninja-release/package-static/bin/spacerabbitd`
- `./spacerabbit-core/build/ninja-debug/spacerabbitd`
- `./spacerabbit-core/build/ninja-debug/package-shared/bin/spacerabbitd`
- `./spacerabbit-core/build/ninja-debug/package-static/bin/spacerabbitd`
- `../spacerabbit-core/build/ninja-debug/spacerabbitd`
- `../spacerabbit-core/build/ninja-debug/package-shared/bin/spacerabbitd`
- `../spacerabbit-core/build/ninja-debug/package-static/bin/spacerabbitd`
- `../spacerabbit-core/build/ninja-release/spacerabbitd`
- `../spacerabbit-core/build/ninja-release/package-shared/bin/spacerabbitd`
- `../spacerabbit-core/build/ninja-release/package-static/bin/spacerabbitd`

You can override the source daemon binary by setting `SPACERABBITD_PATH` in the Xcode scheme
environment or in CI before invoking `xcodebuild`.

If you want Xcode or packaging scripts to build the core repo automatically when the daemon is
missing, set `BUILD_SPACERABBIT_CORE_IF_NEEDED=1`. For a repo-local `spacerabbit-core/` checkout,
the default build hook now runs:

- `make -C spacerabbit-core release`

You can still override that with one of:

- `SPACERABBIT_CORE_BUILD_COMMAND`: shell command run from the app repo root
- `spacerabbit-core/scripts/ci-build-spacerabbitd.sh`: executable helper script in the core repo

Example:

```sh
export BUILD_SPACERABBIT_CORE_IF_NEEDED=1
make build
```

On first launch the app creates `~/Library/Application Support/SpaceRabbit/settings.json` if it
does not already exist, then launches `spacerabbitd --settings <that path>`.

## Distribution

For direct GitHub Releases distribution, ship a signed and notarized DMG as the primary download.
The repo now includes:

- `scripts/package-release.sh` to archive an arm64-only release build, sign it with Developer ID,
  notarize a ZIP of the app, staple the app, build a DMG, then notarize and staple the DMG.
- `scripts/resolve-daemon.sh` to locate `spacerabbitd` from a submodule/sibling checkout or build
  it on demand.
- `.github/workflows/release.yml` to run the same flow on GitHub Actions and attach the DMG, ZIP,
  and SHA-256 checksums to a release tag.

The daemon must be signed separately because it is a nested executable. The release build copies it
into `Contents/Helpers` and signs it before the outer app is signed.

### Release Secrets

Set these repository secrets for GitHub Actions:

- `SUBMODULE_SSH_KEY`: SSH private key with read access to `animaslabs/spacerabbit-core`
- `BUILD_CERTIFICATE_BASE64`: base64-encoded Developer ID Application `.p12`
- `P12_PASSWORD`: password for the `.p12`
- `BUILD_KEYCHAIN_PASSWORD`: temporary keychain password used during the job
- `DEVELOPMENT_TEAM`: your Apple Developer Team ID
- `APPLE_API_KEY_BASE64`: base64-encoded App Store Connect API key `.p8`
- `APPLE_API_KEY_ID`: App Store Connect key ID
- `APPLE_API_ISSUER_ID`: App Store Connect issuer ID for team keys

For an all-in-one CI build, this repo now checks out the `spacerabbit-core` git submodule and
builds it locally before packaging the app. By default it uses the core repo's `make release`
wrapper, and only needs an override if the core build process changes.

Because the submodule URL is SSH-based (`git@github.com:animaslabs/spacerabbit-core.git`), the
workflow expects a `SUBMODULE_SSH_KEY` secret so `actions/checkout` can clone the private submodule
before the build starts.

### Local Signed Build

```sh
export SPACERABBITD_PATH=/absolute/path/to/spacerabbitd
export DEVELOPMENT_TEAM=YOURTEAMID
export CODE_SIGN_IDENTITY="Developer ID Application"
export APPLE_API_KEY_PATH=/absolute/path/to/AuthKey_XXXXXX.p8
export APPLE_API_KEY_ID=XXXXXX
export APPLE_API_ISSUER_ID=xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
make package-release
```

Or, with a repo-local `spacerabbit-core/` checkout:

```sh
export BUILD_SPACERABBIT_CORE_IF_NEEDED=1
make package-release
```
