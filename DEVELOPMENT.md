# Development

This document covers building SpaceHound from source, its architecture, and how
releases are packaged. For end-user documentation, see the [README](README.md).

## Architecture

SpaceHound is a macOS menu bar app (AppKit, Objective-C/Objective-C++) that links
the `spacehound-core` runtime directly into the main app process. There is no
separately launched `spacehoundd` helper during normal app runs.

The app build uses the repo-local `spacehound-core/` sources directly. The app
target compiles the core sources into the main app process, generates
`spacehound/version.hpp` during the build, and uses a vendored
`nlohmann/json.hpp` header from this repo.

The CMake project builds those sources only as an internal static target for
tests and examples. It does not build or install a shared library, so the app
has no separate core binary or public C++ ABI to version.

Key source files:

- `SpaceHoundApp/Sources/AppDelegate.m` — menu bar item, accessibility
  permission workflow, app lifecycle.
- `SpaceHoundApp/Sources/SHRuntimeHost.mm` — hosts the in-process
  `spacehound-core` runtime and drives the menu bar title.
- `SpaceHoundApp/Sources/SHSettingsWindowController.mm` — the Settings window UI
  (toggles and shortcut recorders).
- `SpaceHoundApp/Sources/SHSettingsStore.mm` — reads/writes `settings.json` and
  defines the canonical list of hotkey actions.
- `SpaceHoundApp/Sources/SHLoginItemManager.m` — manages launch-at-login state
  through macOS Service Management.
- `SpaceHoundApp/Sources/SHPermissions.m` — Accessibility access checks.
- `SpaceHoundApp/Sources/SHLogging.m` — local Apple unified-log categories and
  subsystem definitions.
- `spacehound-core/lib/internal/logging.hpp` — the matching internal logging
  definitions used by the C++ runtime.
- `spacehound-core/` — the C++ runtime that performs Space/display/window
  switching. See `spacehound-core/docs/` for the settings schema and API notes.
- Sparkle 2 — checks the signed appcast and safely replaces/relaunches the app.
  Updater preferences are owned by Sparkle in `NSUserDefaults`, not by the
  runtime's `settings.json` schema.
- Sentry Cocoa — captures native crashes in distributed builds once the user
  opts in. Broader Sentry analytics, logs, tracing, replay, screenshots, and
  network instrumentation are disabled.

## Prerequisites

- macOS 14.0 or newer (deployment target)
- Xcode 26.4 or newer
- [XcodeGen](https://github.com/yonaskolb/XcodeGen) 2.46.0 or newer

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

The app launches as a menu bar item and can be exited from `Quit SpaceHound`.

### Local unified logs

SpaceHound writes structured diagnostics to Apple's unified logging system
under subsystem `com.jjkr.spacehound`. Logs are categorized as
`lifecycle`, `permissions`, `navigation`, `settings`, `updates`, `login-item`,
and `crash-reporting`. The app and C++ runtime share this subsystem and
category set.
The entries stay on the Mac and are not forwarded to Sentry.

Stream logs while exercising a development build:

```sh
/usr/bin/log stream --style compact --level debug \
  --predicate 'subsystem == "com.jjkr.spacehound"'
```

Inspect recent persisted entries:

```sh
/usr/bin/log show --last 15m --info --debug --style compact \
  --predicate 'subsystem == "com.jjkr.spacehound"'
```

Dynamic values are private by default. Logs intentionally exclude settings
contents, settings paths, shortcut values, application and window names, URLs,
and localized error descriptions.

### Crash monitoring in local builds

Debug builds do not contain a Sentry DSN and start without crash monitoring. To
exercise Sentry locally, bake the public DSN into the build:

```sh
SENTRY_DSN="https://PUBLIC_KEY@HOST/PROJECT_ID" make run
```

Crash reporting is opt-in. When a DSN is present and no choice has been
recorded, the app asks on launch before the Accessibility prompt; the answer is
stored in `NSUserDefaults` and can be changed with **Send crash reports** in
Settings. Builds without a DSN never ask and show the toggle disabled. To see
the prompt again:

```sh
defaults delete com.jjkr.spacehound SHCrashReportingEnabled
```

When the user has opted in, Sentry is initialized before AppKit starts. Debug
builds report to the `development` environment and Release builds to
`production`. Sentry captures native crashes and uncaught Objective-C
exceptions only; sessions, handled errors, app hangs, breadcrumbs, client
reports, tracing, profiling, logs, screenshots, replay, MetricKit, and default
PII are disabled.

To verify the pipeline end to end, hold Option while opening the menu bar menu
and choose **Test Crash Reporting…**. The item is only enabled while crash
reporting is on. After confirming, the app crashes deliberately; relaunch it to
upload the report, then check the `crash-reporting` log category and the
Sentry project. Launch the app with `make run` or from Finder rather than under
a debugger, because no report is captured while a debugger is attached.

On first launch the app creates
`~/Library/Application Support/SpaceHound/settings.json` if it does not already
exist, then starts the SpaceHound runtime in-process with that settings path.

## Settings schema

The canonical `settings.json` contract shared between the app (writer) and the
runtime (reader) is documented in
[`spacehound-core/docs/settings-json-schema.md`](spacehound-core/docs/settings-json-schema.md).

## Distribution

For the step-by-step release procedure, use the canonical
[release runbook](RELEASING.md). This section documents the underlying packaging
and distribution design.

Release binaries are GitHub Release assets. The Sparkle feed is a single signed
`appcast.xml` served by an assets-only Cloudflare Worker at
`https://updates.spacehound.app/appcast.xml`; release notes are embedded in the
feed, so the Worker serves one file. Beta and production share that feed:
every release is first added as a Sparkle `beta` channel item and a GitHub
pre-release, and promotion removes the channel tag and re-signs the feed
without touching the binaries. The repo includes:

- `scripts/package-release.sh` to archive an arm64-only release build, sign it
  with Developer ID, notarize a ZIP of the app, staple the app, build a DMG, then
  notarize and staple the DMG.
- `scripts/generate-appcast.sh` to merge one release into the published feed as
  a signed beta item, without exposing the private signing key in process
  arguments.
- `scripts/promote-appcast.swift` to move one item from the beta channel to the
  default channel and drop superseded beta items. `sign_update` re-signs the
  feed afterwards.
- `scripts/fetch-live-appcast.sh` and `scripts/resolve-sparkle-tools.sh`,
  shared by both workflows.
- `.github/workflows/release.yml`, triggered by pushing a `vX.Y.Z` tag, to build
  one notarized release, publish it as a GitHub pre-release, and add it to the
  beta channel. `.github/workflows/promote.yml`, triggered when that pre-release
  is changed to a release, to promote those exact bytes.
- `release-notes/vX.Y.Z.md` files for reviewed, user-facing Sparkle and GitHub
  Release notes. Copy `release-notes/TEMPLATE.md` when preparing a version.
- `updates/` for the Cloudflare Worker configuration. `updates/public/appcast.xml`
  is written by the workflows and is not committed. `robots.txt` and the
  `X-Robots-Tag: noindex` header in `_headers` keep the host out of search
  and AI crawler indexes.

Both workflows share one concurrency group and start by fetching the published
feed, so two release operations can never interleave their edits. A release is
rejected unless its version is greater than every version already in the feed.
Appcast generation also derives the public key from the private signing secret
and refuses to continue unless it matches `SPARKLE_PUBLIC_ED_KEY` embedded in
the app.

The release app is packaged as a single executable bundle. There is no nested
SpaceHound daemon helper to copy or sign separately. Sparkle's framework and
installer helpers are embedded and signed by Xcode.

### Version contract

A release is identified by its Git tag `vX.Y.Z`. The app gets
`CFBundleShortVersionString=X.Y.Z` and `CFBundleVersion=X.Y.Z`; Sparkle
compares `CFBundleVersion`, so every release, including a beta that only fixes
a previous beta, uses a new `X.Y.Z`. Artifacts are named
`SpaceHound-X.Y.Z-arm64.zip` and `SpaceHound-X.Y.Z-arm64.dmg`. The exact same
ZIP is first offered to beta users and later promoted to production. A beta
that should not ship is simply never promoted; the correction is the next
version.

### Release secrets

The personal Apple Developer team must own the explicit macOS App ID
`com.jjkr.spacehound`. Export that team's **Developer ID Application**
certificate together with its private key as a password-protected `.p12`; the
promote workflow rejects archives with any other bundle identifier.

Both workflows run in the `release` GitHub environment. Set these secrets on
it:

- `BUILD_CERTIFICATE_BASE64`: base64-encoded Developer ID Application `.p12`
- `P12_PASSWORD`: password for the `.p12`
- `BUILD_KEYCHAIN_PASSWORD`: temporary keychain password used during the job
- `DEVELOPMENT_TEAM`: your Apple Developer Team ID
- `APPLE_ID`: Apple ID used for notarization
- `APPLE_APP_SPECIFIC_PASSWORD`: app-specific password for that Apple ID
- `SPARKLE_ED_PRIVATE_KEY`: the exported Sparkle private seed
- `SENTRY_AUTH_TOKEN`: Sentry organization token with `org:ci` access, used only
  by `sentry-cli` to upload release dSYMs
- `CLOUDFLARE_API_TOKEN`: token with Workers edit permission for the account
  and the `spacehound.app` zone
- `CLOUDFLARE_ACCOUNT_ID`: the Cloudflare account that owns the zone

And these variables:

- `SPARKLE_PUBLIC_ED_KEY`: the matching base64 public key.
- `SENTRY_DSN`: the public DSN for the `jjkr/spacehound` Sentry project.

Promotion is whoever can edit GitHub releases in the repository. For an
explicit approval step, add a required reviewer to the `release` environment;
both workflows will then wait for approval before running.

Keep an encrypted offline backup of the Sparkle private key. Do not print it,
place it in command arguments, or commit it. Losing it prevents new signed-feed
updates until a deliberate key-recovery or rotation release is performed.

### Local signed build

```sh
export DEVELOPMENT_TEAM=YOURTEAMID
export CODE_SIGN_IDENTITY="Developer ID Application"
export RELEASE_VERSION=0.4.0
export SPARKLE_PUBLIC_ED_KEY="YOUR_PUBLIC_KEY"
export SENTRY_DSN="YOUR_PUBLIC_SENTRY_DSN"
export SENTRY_AUTH_TOKEN="YOUR_SENTRY_ORG_TOKEN"
export APPLE_API_KEY_PATH=/absolute/path/to/AuthKey_XXXXXX.p8
export APPLE_API_KEY_ID=XXXXXX
export APPLE_API_ISSUER_ID=xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
make package-release
```

Push a `vX.Y.Z` tag on `main` to release. The workflow publishes the beta
first. Testers enable **Receive beta updates** in Settings and validate it.
When it passes, change the GitHub pre-release to a release; the promote
workflow verifies the published archive against the feed, moves the item to the
default channel, re-signs the feed, and deploys it. Leave the pre-release flag
set to keep a version beta-only.

If the release job fails after creating the GitHub pre-release but before
deploying the feed, that release is not visible to Sparkle. Delete the
pre-release and its tag, fix the problem, and push the tag again. Never delete
a release that has appeared in the feed; publish a higher version instead.

Run the local validation suite with:

```sh
make release-script-tests
```
