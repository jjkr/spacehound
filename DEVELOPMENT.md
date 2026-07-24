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
- Sentry Cocoa — captures native crashes in distributed builds. Broader Sentry
  analytics, logs, tracing, replay, screenshots, and network instrumentation are
  disabled.

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

The app launches as a menu bar item and can be exited from `Quit SpaceHound`.

### Local unified logs

SpaceHound writes structured diagnostics to Apple's unified logging system
under subsystem `com.jjkr.spacehound`. Logs are categorized as
`lifecycle`, `permissions`, `navigation`, `settings`, `updates`, and
`login-item`. The app and C++ runtime share this subsystem and category set.
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
exercise Sentry locally, provide the public DSN only for the launched process:

```sh
SENTRY_DSN="https://PUBLIC_KEY@HOST/PROJECT_ID" make run
```

Sentry is initialized before AppKit starts. It captures native crashes and
uncaught Objective-C exceptions only; sessions, handled errors, app hangs,
breadcrumbs, client reports, tracing, profiling, logs, screenshots, replay,
MetricKit, and default PII are disabled.

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

Production updates are hosted at `https://updates.spacehound.app`; beta
updates use `https://beta-updates.spacehound.app`. Each environment has its
own private S3 bucket, CloudFront distribution, certificate, hosted zone, and
GitHub publisher role. GitHub Releases contains the production mirror. The repo
includes:

- `scripts/package-release.sh` to archive an arm64-only release build, sign it
  with Developer ID, notarize a ZIP of the app, staple the app, build a DMG, then
  notarize and staple the DMG.
- `scripts/generate-appcast.sh` to generate and verify a signed Sparkle appcast
  without exposing the private signing key in process arguments.
- `.github/workflows/release.yml` to build one notarized candidate and publish it
  to beta, plus `.github/workflows/promote-release.yml` to manually approve and
  promote those exact bytes.
- `release-notes/vX.Y.Z.md` files for reviewed, user-facing Sparkle and GitHub
  Release notes. Copy `release-notes/TEMPLATE.md` when preparing a version.
- `infra/` for the self-mutating CDK Pipeline and independent beta/production
  stacks. See
  [`infra/README.md`](infra/README.md) for the one-time setup.

The workflow is globally serialized before checking published version ordering,
so concurrent jobs cannot roll either mutable feed or `latest` alias back.
Appcast generation also derives the public key from the private signing secret
and refuses to continue unless it matches `SPARKLE_PUBLIC_ED_KEY` embedded in
the app.

The release app is packaged as a single executable bundle. There is no nested
SpaceHound daemon helper to copy or sign separately. Sparkle's framework and
installer helpers are embedded and signed by Xcode.

### Version contract

Release inputs use a marketing version `X.Y.Z` and a final-candidate number
`N` from 1 through 255. The app gets `CFBundleShortVersionString=X.Y.Z` and
`CFBundleVersion=X.Y.ZfcN`; candidate artifacts use `X.Y.Z-fcN` in their names.
The exact same ZIP and DMG are first published to beta and later promoted to
production. Promotion creates the stable `vX.Y.Z` Git tag but does not rebuild
the app. A beta-only candidate can be corrected with a higher candidate number.
After `X.Y.Z` is promoted, that marketing version cannot be reused; corrections
must use a higher marketing version.

A legacy appcast version written as plain `X.Y.Z` sorts after every `X.Y.ZfcN`.
If `0.1.0` was already published by the old workflow, start this candidate flow
at `0.1.1fc1`, not `0.1.0fc1`.

### Release secrets

The personal Apple Developer team must own the explicit macOS App ID
`com.jjkr.spacehound`. Export that team's **Developer ID Application**
certificate together with its private key as a password-protected `.p12`; the
release workflow rejects promoted artifacts with any other bundle identifier.

Set these secrets on the `beta` GitHub environment, because the beta job is the
only job that builds, signs, notarizes, and creates appcasts:

- `BUILD_CERTIFICATE_BASE64`: base64-encoded Developer ID Application `.p12`
- `P12_PASSWORD`: password for the `.p12`
- `BUILD_KEYCHAIN_PASSWORD`: temporary keychain password used during the job
- `DEVELOPMENT_TEAM`: your Apple Developer Team ID
- `APPLE_ID`: Apple ID used for notarization
- `APPLE_APP_SPECIFIC_PASSWORD`: app-specific password for that Apple ID
- `SPARKLE_ED_PRIVATE_KEY`: the exported Sparkle private seed
- `SENTRY_AUTH_TOKEN`: Sentry organization token with `org:ci` access, used only
  by `sentry-cli` to upload release dSYMs

Set these variables separately on both `beta` and `production` environments,
using the outputs from that environment's CDK stack:

- Variable `SPARKLE_PUBLIC_ED_KEY`: the matching base64 public key.
- Variable `AWS_RELEASE_ROLE_ARN`: CDK output `GitHubPublisherRoleArn`.
- Variable `AWS_RELEASE_BUCKET`: CDK output `ArtifactBucketName`.
- Variable `AWS_CLOUDFRONT_DISTRIBUTION_ID`: CDK output `DistributionId`.
- Variable `AWS_RELEASE_REGION`: `us-east-1`.
- Variable `SENTRY_DSN`: the public DSN for the `jjkr/spacehound` Sentry
  project. Only `beta` needs it because production promotes the same app bytes.

The production environment does not need the certificate, Apple credentials,
or Sparkle private key. The separately dispatched **Promote release** workflow
is the release gate and rejects any actor other than `jjkr`. Repository-level
secrets may be used instead, but keeping the signing material scoped to `beta`
makes the build-once boundary explicit.

Keep an encrypted offline backup of the Sparkle private key. Do not print it,
place it in command arguments, or commit it. Losing it prevents new signed-feed
updates until a deliberate key-recovery or rotation release is performed.

### Local signed build

```sh
export DEVELOPMENT_TEAM=YOURTEAMID
export CODE_SIGN_IDENTITY="Developer ID Application"
export RELEASE_VERSION=0.1.0
export RELEASE_BUILD_VERSION=0.1.0fc1
export SPARKLE_PUBLIC_ED_KEY="YOUR_PUBLIC_KEY"
export SENTRY_DSN="YOUR_PUBLIC_SENTRY_DSN"
export SENTRY_AUTH_TOKEN="YOUR_SENTRY_ORG_TOKEN"
export APPLE_API_KEY_PATH=/absolute/path/to/AuthKey_XXXXXX.p8
export APPLE_API_KEY_ID=XXXXXX
export APPLE_API_ISSUER_ID=xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
make package-release
```

Dispatch **Release candidate** from the `main` branch with `version=X.Y.Z` and
`candidate=N`. The workflow publishes beta first. Testers enable **Receive Beta
Updates** in the menu bar and validate the candidate. When it passes, dispatch
**Promote release** with the successful candidate run ID and the same version
and candidate number. Promotion is restricted to `jjkr`; it checks out the
candidate commit, downloads and verifies that run's retained artifact, publishes
the production appcast, and creates `vX.Y.Z` plus its GitHub Release. Do not run
promotion to leave the candidate beta-only.

If a job fails after uploading versioned objects but before publishing the
appcast, that release is not visible to Sparkle. Confirm the appcast still
points to the previous version, then remove only the orphaned version prefix
before retrying. S3 versioning retains removed object versions for recovery.
Never remove or replace a prefix that has appeared in an appcast; publish a
higher candidate for a beta-only correction or a higher marketing version after
production promotion.

Run the local validation suites with:

```sh
make release-script-tests
make infra-install
make infra-test
```
