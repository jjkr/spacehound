# SpaceHound Production Readiness

Last reviewed: July 18, 2026

## Executive summary

SpaceHound's binary delivery chain is in strong shape. The current release is
Developer ID signed, notarized, stapled, protected by the hardened runtime, and
distributed through a signed Sparkle feed. Release artifacts are built once,
published to a beta channel first, and promoted without rebuilding. Binaries
are hosted as GitHub Release assets and the feed by a Cloudflare Worker, so
there is no bespoke infrastructure to operate.

The remaining work is concentrated in product behavior and launch operations:

- Verify the Accessibility workflow on clean machines.
- Make Apple Silicon-only support explicit, or produce and test a universal build.
- Test every supported macOS release because core behavior depends on private APIs.
- Add pull-request CI and protect the release path.
- Add diagnostics, support surfaces, privacy information, and third-party notices.
- Add monitoring and incident-response procedures around the update service.

The website itself is being built separately. This document lists the public
information that the website must expose, but does not cover its implementation.

## Launch blockers

### 1. Crash monitoring and privacy verification

The obsolete telemetry setting has been removed. Distributed builds use a
privacy-limited Sentry configuration for crashes and uncaught Objective-C
exceptions only. Crash reporting is opt-in: the app asks on first launch, the
choice is editable with **Send crash reports** in Settings, and nothing is
sent until the user agrees. Debug builds report to the `development`
environment so local testing never pollutes production issues. Holding Option
in the menu bar menu reveals **Test Crash Reporting…**, which crashes the app
deliberately to verify the pipeline. Release packaging uploads dSYMs and fails
if symbol upload is unavailable.

Before launch, enable Sentry's default data scrubbing and IP-address scrubbing,
publish the exact crash-data disclosure, and use **Test Crash Reporting…** on a
signed beta to verify that a correctly symbolicated event arrives. Do not enable
broader Sentry collection without a separate privacy review.

### 2. State the hardware requirement accurately

The distributed release is arm64-only, while the README currently states only
that macOS 14 or newer is required.

Before launch, either:

- Advertise **Apple Silicon Mac with macOS 14 or newer** everywhere; or
- Produce a universal build and test Intel behavior on all claimed OS versions.

The website download button, installation page, release notes, README, and
support responses must agree. Do not let Intel users discover incompatibility
only after downloading the DMG.

### 3. Complete a real compatibility matrix

SpaceHound relies on unsupported and undocumented system behavior, including:

- Private SkyLight/CGS APIs for Space queries.
- Undocumented `CGEvent` gesture fields.
- The private `_AXUIElementGetWindow` function.

These dependencies are the application's largest ongoing reliability risk. The
macOS 27-specific work in version 0.3.0 demonstrates that OS releases can change
the behavior.

Test the signed beta release—not only a local debug build—on every OS and
hardware combination being advertised. At minimum, cover macOS 14, 15, 26, and
27 while they remain supported.

The matrix should include:

- One, two, and three displays.
- **Displays have separate Spaces** enabled and disabled.
- Horizontal and vertical display arrangements.
- Display connect/disconnect, sleep/wake, lock/unlock, and resolution changes.
- Full-screen applications, Stage Manager, hidden menu bar, and displays with no
  open windows.
- Every workspace and display navigation action.
- Window focus cycling across different applications and window types.
- Mission Control and App Expose actions.
- Fast swipe at the first and last Space.
- Rapid repeated shortcuts and gestures.
- Non-US keyboard layouts.
- Clean installation, update from the previous production release, and uninstall.
- Accessibility denial, grant, revocation, and re-grant.
- Accessibility-only operation for hotkeys, gestures, synthetic input, and
  window focus.

Record the tested matrix and known limitations for every release.

### 4. Add continuous integration for normal changes

The existing GitHub Actions workflows build and promote releases manually. Pull
requests and ordinary pushes do not automatically run the app and core checks.

Add a pull-request and main-branch workflow that runs:

```sh
make -C spacehound-core test
make release-script-tests
make build
xcodebuild -project SpaceHound.xcodeproj \
  -scheme SpaceHound \
  -configuration Release \
  -derivedDataPath build/DerivedDataAnalyze \
  CODE_SIGNING_ALLOWED=NO \
  analyze
```

Also:

- Add a root `make verify` target that matches CI.
- Build both Debug and Release configurations.
- Treat warnings as errors where practical.
- Add caching without allowing stale generated files to hide failures.
- Require CI to pass before merge.
- Add app-layer tests for settings, permissions, menu state, updater channel
  selection, and shortcut validation.

### 5. Protect the source and release path

At review time, the GitHub repository was private and branch protection was not
available under its current plan. Promotion is gated only by who can edit
GitHub releases.

Before launch:

- Enable branch protection or repository rulesets for `main`, and protect
  `v*` tags so only release maintainers can create them.
- Require pull requests, review, and passing CI.
- Add a required reviewer to the `release` GitHub environment so every release
  and promotion waits for an explicit approval.
- Add `CODEOWNERS` for `.github/workflows/`, `scripts/`, `project.yml`, and
  `updates/`.
- Require strong account security and recovery for release maintainers.
- Pin GitHub Actions to immutable commit SHAs.
- Pin Xcode and Homebrew-installed build-tool versions used by releases.
- Enable Dependabot or Renovate for Swift packages and GitHub Actions.
- Enable secret scanning and push protection where the repository plan permits.

The Cloudflare API token should be scoped to Workers on the single account and
zone, and nothing else. Preserve that property.

### 6. Add diagnostics and supportability

Runtime action failures are currently written to `stderr`. A normally launched
menu-bar app gives the user and support operator no practical way to retrieve
those messages.

Before launch:

- Use Unified Logging through `os_log` for lifecycle, permission, update, and
  action failures. Never log pressed keys, window titles, or other sensitive
  content by default.
- Add an **About SpaceHound** surface with the marketing version, build version,
  architecture, macOS version, and update channel.
- Add **Copy Diagnostics** with a reviewed, privacy-safe payload.
- Add **Report a Problem** and **Support** actions.
- Preserve the dSYM generated for every production build in Sentry and a private,
  access-controlled release artifact.
- Verify logs and symbols against a beta build crashed with **Test Crash
  Reporting…** before relying on them.

### 7. Complete legal, privacy, and public-support basics

The application embeds Sparkle, Sentry, and nlohmann/json, but the shipped
bundle does not include third-party notices.

Before launch:

- Include the required Sparkle, Sentry, and nlohmann/json copyright and license
  notices in the bundle or an in-app acknowledgements view.
- Publish a privacy policy that describes Sentry crash reports, automatic update
  requests, and any server logs retained by the update service while clearly
  stating that the app collects no usage analytics.
- Establish a monitored support email or public issue tracker.
- Publish a security contact and vulnerability-reporting process.
- If the source repository becomes public, add an explicit project `LICENSE`,
  `SECURITY.md`, and contribution expectations.
- If the repository remains private, remove the README claim that releases are
  publicly mirrored in that private repository, or create a separate public
  releases repository.
- If the product is paid, publish pricing, refund, tax, and license terms before
  accepting payment.

## Important product work before broad promotion

These items may not block a small controlled launch, but should be completed
before significant marketing.

### Hotkey safety

- Detect duplicate shortcuts before saving.
- Define deterministic behavior if a duplicate reaches the runtime anyway.
- Prevent or clearly warn about global shortcuts without modifiers, since a
  binding such as plain `A` can swallow normal typing system-wide.
- Test shortcuts across common keyboard layouts and input sources.
- Explain conflicts with macOS and other global-shortcut utilities.

### Settings recovery

- Offer **Back Up and Reset to Defaults** when `settings.json` is malformed.
- Preserve the invalid file for diagnosis instead of overwriting it silently.
- Test a read-only settings directory, truncated JSON, unsupported schema
  version, and disk-full behavior.
- Keep settings writes atomic.

### Menu-bar lifecycle

- Verify **Launch at login** registration, approval, disabling, and persistence
  using an installed signed build.
- Test app relaunch after update, login, crash, sleep/wake, and Fast User
  Switching.
- Ensure the status item remains understandable when the menu bar is crowded.
- Provide a visible version and quit path even when the runtime cannot start.

### Accessibility and usability

- Test VoiceOver labels and reading order.
- Test complete keyboard-only operation of Settings.
- Test increased contrast, reduced motion, and larger accessibility text sizes.
- Review contrast and disabled-control states.
- Decide whether launch is English-only and state that clearly.

### Support policy

- Define which macOS releases are supported and for how long.
- Define the response target for broken updates and private-API regressions.
- Publish known limitations, especially around private APIs, display layouts,
  and keyboard utilities.
- Keep beta participation explicitly opt-in.

## Release and supply-chain improvements

The existing release architecture already has several valuable controls:

- Each release is built, signed, and notarized once.
- Production promotion reuses the exact beta bytes: it only edits the feed.
- Sparkle archives and the feed are signed with Ed25519, so neither GitHub nor
  Cloudflare needs to be trusted for integrity.
- The embedded public key is verified during release generation and promotion.
- Promotion downloads the published archive and verifies its feed signature,
  length, code signature, Gatekeeper acceptance, notarization ticket, and
  bundle metadata before touching the feed.
- Release operations share a global concurrency lock and always start from the
  published feed, so a release cannot silently drop earlier items.
- Versions are strictly increasing and never reused.

Additional improvements:

- Preserve release dSYMs and a machine-readable build manifest.
- Generate an SBOM for embedded and build-time dependencies.
- Consider GitHub artifact attestations or equivalent provenance.
- Test a Sparkle signing-key restore from the encrypted offline backup.
- Write and rehearse a Sparkle signing-key rotation procedure.
- Monitor Developer ID certificate expiration well before release day.
- Keep a known-good previous signed artifact readily available.
- Add a pre-promotion checklist sign-off tied to the release tag.
- Add an explicit hotfix procedure. Sparkle users cannot be reliably rescued by
  silently pointing the feed at an older version; a bad public release normally
  requires a higher-version fixed release.

## Infrastructure and operations

### Existing strengths

The update delivery path currently provides:

- Release binaries on GitHub Releases: no bandwidth cost, no bucket to secure,
  and release assets are immutable by convention.
- A single-file feed on an assets-only Cloudflare Worker with a five-minute
  cache TTL, so promotions reach users quickly.
- Deployment history in Cloudflare, so a bad feed deploy can be rolled back
  from the dashboard in one step.
- A copy of every deployed feed kept as a workflow artifact for 30 days.
- No infrastructure code to maintain. The earlier AWS design (per-environment
  S3, CloudFront, Route 53, OIDC roles, synthetic canaries, and CloudWatch
  alarms) was removed deliberately; the signed feed and signed archives make
  the hosting untrusted, and the operational surface was out of proportion to
  a single-developer app.

### Add before or shortly after launch

- An external uptime check on `https://updates.spacehound.app/appcast.xml`
  and the latest download URL, with notifications. This replaces the removed
  canaries.
- Alerts for failed release and promote workflow runs.
- Monitoring for Apple certificate and GitHub credential failures.
- A documented incident procedure for:
  - Broken or unavailable appcasts.
  - A bad application release.
  - Compromised GitHub, Cloudflare, Apple, or Sparkle credentials.
  - Loss of a signing key or certificate.

WAF and rate limiting are not requirements for static signed downloads served
by GitHub and Cloudflare at the expected launch scale.

## Website and public documentation requirements

The website implementation is out of scope for this document, but it must expose:

- A prominent direct DMG download over HTTPS.
- The exact hardware and macOS requirements.
- Installation and Accessibility instructions.
- A short explanation of why Accessibility access is needed.
- Current version and release notes.
- Privacy policy.
- Support contact and troubleshooting page.
- Uninstall instructions, including optional settings and permission cleanup.
- Known limitations and supported macOS versions.
- Pricing/refund/license terms if applicable.
- A way to verify checksums for users who want it.

Do not link unauthenticated users to releases or issues in a private repository.

## Release acceptance checklist

Use this checklist for every public release.

### Code and automation

- [ ] Pull-request CI passed on the tagged commit.
- [ ] Core tests passed with no unexpected skips.
- [ ] App Debug and Release builds passed.
- [ ] Xcode static analysis passed.
- [ ] Release-script tests passed.
- [ ] Dependency and license scans passed.
- [ ] Release notes were reviewed and contain no placeholder text.
- [ ] The tag points at the reviewed commit on `main`.

### Signed release

- [ ] Developer ID signature validates deeply and strictly.
- [ ] Gatekeeper accepts the app.
- [ ] App and DMG notarization tickets validate.
- [ ] Hardened runtime is enabled.
- [ ] Bundle identifier and version are correct.
- [ ] Minimum macOS version and architecture match public requirements.
- [ ] Sparkle feed URL is correct.
- [ ] Sparkle public key and signed feed validate.
- [ ] dSYM and build manifest are retained privately.

### Functional acceptance

- [ ] Clean install and first-launch onboarding passed.
- [ ] Accessibility-only operation passed for hotkeys, gestures, synthetic input,
      and window focus.
- [ ] The supported OS/display matrix passed.
- [ ] Settings save, reload, corruption recovery, and reset passed.
- [ ] Every default hotkey passed.
- [ ] Hotkey recording and conflict handling passed.
- [ ] Fast swipe boundaries passed.
- [ ] Sleep/wake and display hot-plug passed.
- [ ] Launch at login enable, approval, login launch, and disable flows passed.
- [ ] CPU, memory, and energy impact were checked during extended use.
- [ ] VoiceOver and keyboard-only Settings operation passed.

### Update acceptance

- [ ] The feed reports the intended version on the beta channel.
- [ ] The GitHub pre-release DMG and ZIP download successfully.
- [ ] Update from the previous production version succeeds with beta enabled.
- [ ] The app relaunches and retains settings.
- [ ] With beta disabled, the version is not offered.
- [ ] A human explicitly approved the tag for promotion.
- [ ] The feed and the GitHub "latest" release were verified after promotion.
- [ ] External monitors remain green after promotion.

### Launch operations

- [ ] Support contact is monitored.
- [ ] Privacy policy and third-party notices are published.
- [ ] Incident and hotfix procedures are accessible to the maintainer.
- [ ] Signing-key and certificate backups are verified.
- [ ] Endpoint monitors are active.
- [ ] The previous known-good release is retained.

## Verification performed during this review

The following checks passed on July 18, 2026:

- All 79 native/core tests passed. Three environment-dependent test cases were
  skipped by their own test preconditions.
- Release-script tests passed.
- The unsigned Debug application build succeeded.
- Xcode Release static analysis succeeded with no analyzer diagnostics.
- The (since replaced) AWS-hosted appcasts returned HTTP 200 and served
  version `0.3.0fc1`; the GitHub/Cloudflare path has not yet shipped a release.
- The published production ZIP contained an arm64 app with a macOS 14 minimum.
- Deep code-signature validation passed.
- Gatekeeper accepted the app as Notarized Developer ID software.
- The hardened-runtime signature and stapled notarization ticket validated.
- The distributed app contained the expected Sparkle feed URL and public key.

## Recommended implementation order

1. Verify Sentry privacy settings and symbolication with **Test Crash
   Reporting…** on a signed beta.
2. Verify Accessibility-only operation on clean supported macOS installations.
3. Decide and publish the Apple Silicon/Intel support policy.
4. Add PR CI, branch protection, environment approval, and dependency automation.
5. Add structured logs, About/Diagnostics, dSYM retention, and support links.
6. Add third-party notices, privacy policy, and public security/support contacts.
7. Execute and record the full supported-OS and display matrix.
8. Add external endpoint monitoring, workflow failure alerts, and incident
   runbooks.
9. Ship a beta, test the update from the prior release, and promote the exact
   approved bytes.
