# Releasing SpaceHound

This is the operational runbook for publishing SpaceHound. For release
architecture and local packaging details, see [DEVELOPMENT.md](DEVELOPMENT.md).
For initial AWS, DNS, OIDC, and GitHub environment setup, see
[infra/README.md](infra/README.md).

## Release model

Every release has two identifiers:

- Marketing version: `X.Y.Z`, such as `0.3.0`.
- Final-candidate number: `N` from 1 through 255, such as `1`.

Together they produce bundle version `X.Y.ZfcN` and artifact version
`X.Y.Z-fcN`. The **Release candidate** workflow builds, signs, and notarizes the
app once, then publishes it to the beta feed. After testing, the separately
dispatched **Promote release** workflow publishes those exact bytes to
production. Promotion never rebuilds the app.

The production tag is `vX.Y.Z`. Once a marketing version is promoted, it cannot
be reused. A beta-only candidate may be replaced by a higher candidate number
for the same marketing version.

## One-time prerequisites

Before the first release, complete the infrastructure setup in
[infra/README.md](infra/README.md) and confirm:

- The `beta` and `production` GitHub environments exist.
- Each environment has `AWS_RELEASE_REGION`, `AWS_RELEASE_BUCKET`,
  `AWS_CLOUDFRONT_DISTRIBUTION_ID`, `AWS_RELEASE_ROLE_ARN`, and
  `SPARKLE_PUBLIC_ED_KEY` variables populated from its CDK stack outputs.
- The `beta` environment has the public `SENTRY_DSN` variable and the private
  `SENTRY_AUTH_TOKEN` secret. The token must have `org:ci` access.
- The `jjkr/spacehound` Sentry project has default data scrubbing enabled
  and **Prevent Storing of IP Addresses** turned on under Security & Privacy.
- The candidate workflow can access the Developer ID certificate, Apple
  notarization, temporary keychain, and Sparkle private-key secrets listed in
  [DEVELOPMENT.md](DEVELOPMENT.md#release-secrets). Prefer scoping them to the
  `beta` environment; repository secrets are also supported.
- The production environment has no private signing material.
- An encrypted offline backup of the Sparkle private key exists.
- The AWS infrastructure pipeline has successfully deployed both update stacks.

The promotion workflow is the manual release gate. It currently permits only
the GitHub user `jjkr` to promote a candidate.

## 1. Prepare the release

Choose a marketing version and start its candidate number at `1`. Copy the
release-note template to a file named for that marketing version, write the
user-facing notes, and remove the `RELEASE_NOTES_PLACEHOLDER` comment:

```sh
cp release-notes/TEMPLATE.md release-notes/v0.3.0.md
```

Use second-level headings in the authored file; the workflow adds the release
title. Commit the notes with the release changes so they can be reviewed and so
the candidate commit permanently records what was published.

Preview and validate the beta and production note files locally:

```sh
./scripts/prepare-release-notes.sh \
  0.3.0 \
  0.3.0fc1 \
  release-notes/v0.3.0.md \
  build/release-notes-preview
```

Confirm the desired commit is on `main`, the branch is synchronized with
GitHub, and the working tree is clean:

```sh
git switch main
git pull --ff-only
git status --short
```

Run the release checks. This example prepares `0.3.0` candidate `1`:

```sh
make release-script-tests
./scripts/validate-release-version.sh 0.3.0 0.3.0fc1
./scripts/check-release-availability.sh jjkr/spacehound 0.3.0
```

The availability check uses the GitHub CLI, so `gh auth status` must succeed.
Do not proceed if the tag or release already exists.

## 2. Build and publish the beta candidate

From GitHub, open **Actions**, choose **Release candidate**, select **Run
workflow**, use the `main` branch, and enter the marketing version and candidate
number.

The equivalent CLI command is:

```sh
gh workflow run release.yml \
  --repo jjkr/spacehound \
  --ref main \
  -f version=0.3.0 \
  -f candidate=1
```

Find and monitor the run:

```sh
gh run list \
  --repo jjkr/spacehound \
  --workflow release.yml \
  --event workflow_dispatch \
  --limit 5

gh run watch CANDIDATE_RUN_ID \
  --repo jjkr/spacehound \
  --interval 10 \
  --exit-status
```

Save the numeric candidate run ID. A successful run publishes:

- Immutable beta artifacts under `releases/vX.Y.Z-fcN/`.
- Beta `latest` aliases and the beta appcast.
- A retained GitHub Actions artifact named `SpaceHound-X.Y.Z-fcN` containing
  the candidate, authored release notes, and production appcast. It is retained
  for 30 days.
- The archive's dSYMs to `jjkr/spacehound`. Missing credentials, missing
  symbols, or a failed upload stops the candidate before publication.

## 3. Verify the beta

Do not promote until the beta candidate has been approved. At minimum:

- Confirm `https://beta-updates.spacehound.app/appcast.xml` names the
  expected `X.Y.ZfcN` bundle version.
- Confirm the latest beta DMG is reachable at
  `https://beta-updates.spacehound.app/releases/latest/SpaceHound-arm64.dmg`.
- In an installed copy of SpaceHound, enable **Receive Beta Updates**, choose
  **Check for Updates…**, and install the candidate.
- Confirm the update signature is accepted, installation completes, the app
  relaunches, and its core behavior works.
- For the first monitored release, use a disposable pre-release build with a
  temporary intentional crash, relaunch it to send the cached event, and confirm
  Sentry shows the expected release/build with symbolicated SpaceHound frames.
  Remove the crash trigger before building the candidate that may be published.
- Record explicit approval to promote this candidate.

Basic endpoint checks can be run with:

```sh
curl --fail --show-error \
  https://beta-updates.spacehound.app/appcast.xml
curl --fail --show-error --head \
  https://beta-updates.spacehound.app/releases/latest/SpaceHound-arm64.dmg
```

## 4. Promote the approved candidate

From GitHub, open **Actions**, choose **Promote release**, select **Run
workflow**, use the `main` branch, and enter:

- The successful candidate workflow run ID.
- The same marketing version used for the candidate.
- The same candidate number used for the candidate.

The equivalent CLI command is:

```sh
gh workflow run promote-release.yml \
  --repo jjkr/spacehound \
  --ref main \
  -f candidate_run_id=CANDIDATE_RUN_ID \
  -f version=0.3.0 \
  -f candidate=1
```

Monitor the promotion:

```sh
gh run list \
  --repo jjkr/spacehound \
  --workflow promote-release.yml \
  --event workflow_dispatch \
  --limit 5

gh run watch PROMOTION_RUN_ID \
  --repo jjkr/spacehound \
  --interval 10 \
  --exit-status
```

Promotion validates the source workflow, commit, manifest, signatures,
notarization tickets, embedded versions, feed URLs, and Sparkle public key. It
then publishes the immutable production prefix `releases/vX.Y.Z/`, creates the
stable `vX.Y.Z` tag and GitHub Release, updates the production `latest` aliases
and appcast, and invalidates the relevant CloudFront paths.

## 5. Verify production

Confirm the production endpoints and GitHub Release:

```sh
curl --fail --show-error \
  https://updates.spacehound.app/appcast.xml
curl --fail --show-error --head \
  https://updates.spacehound.app/releases/latest/SpaceHound-arm64.dmg
gh release view v0.3.0 --repo jjkr/spacehound
```

Also disable **Receive Beta Updates** in a stable installation, choose **Check
for Updates…**, and confirm it sees and installs the new production release.
Verify that the GitHub tag targets the commit recorded by the candidate run.

## Corrections and recovery

Use these rules to avoid replacing bytes that may already have been consumed:

| Situation | Action |
| --- | --- |
| Candidate fails before immutable beta artifacts are uploaded | Fix the cause and rerun the same candidate if no candidate objects or appcast entry exist. |
| Candidate fails after immutable upload but before the beta appcast changes | Confirm the appcast still names the previous version. Remove only the orphaned beta version prefix, then retry. |
| Candidate appears in the beta appcast but needs correction | Keep the published candidate and dispatch a higher candidate number for the same marketing version. |
| Promotion artifact has expired | Build a new candidate. Candidate workflow artifacts are retained for 30 days. |
| Promotion fails before the production appcast changes | Inspect the GitHub tag, release, and production prefix before retrying. Remove only an orphaned prefix that was never published in the appcast and has no tag or release. |
| Marketing version already has a production tag, release, or appcast entry | Use a higher marketing version. Never replace the published version. |

S3 versioning provides recovery for removed objects, but deletion is still an
exceptional operation. Resolve the exact bucket, prefix, appcast state, GitHub
tag, and GitHub Release before removing anything. Never overwrite or delete an
immutable prefix that has appeared in an appcast.

## Key-handling rules

- Never print, commit, or put the Sparkle private key in a command argument.
- Keep build, Apple notarization, and Sparkle private-key secrets scoped to the
  beta build environment.
- Keep production limited to public verification data and its OIDC publisher
  role.
- Rotate or recover a signing key only through a separately planned release;
  losing the current private key prevents normal signed updates.
