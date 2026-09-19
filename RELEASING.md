# Releasing SpaceHound

This is the operational runbook for publishing SpaceHound. For release
architecture and local packaging details, see [DEVELOPMENT.md](DEVELOPMENT.md).

## Release model

Every release is a Git tag `vX.Y.Z` on `main`. Pushing the tag runs the
**Release** workflow, which builds, signs, and notarizes the app once, publishes
it as a GitHub **pre-release**, and adds it to the **beta** channel of the
Sparkle feed at `https://updates.spacehound.app/appcast.xml`. Only users who
have enabled **Receive Beta Updates** see it.

After testing, changing the GitHub pre-release into a release runs the
**Promote** workflow, which moves that version to the production channel of the
same feed. Promotion never rebuilds or re-uploads anything; production users
install the exact bytes beta users tested.

Versions are never reused. If a beta needs a fix, it stays a pre-release
forever and the fix ships as the next `X.Y.Z`.

## One-time prerequisites

### Cloudflare

The feed is an assets-only Cloudflare Worker defined in `updates/wrangler.jsonc`
and bound to `updates.spacehound.app` in the existing `spacehound.app` zone.

Create an API token with the **Edit Cloudflare Workers** template plus
**Zone → DNS → Edit**, scoped to the account and the `spacehound.app` zone.
Note the account ID from the Cloudflare dashboard.

Then deploy the Worker once with no feed, which creates the custom domain and
certificate. The release workflow refuses to run while the host does not
resolve, because it cannot tell a missing domain from an outage:

```sh
make updates-login
make updates-deploy
curl -sI https://updates.spacehound.app/appcast.xml | head -1   # HTTP/2 404
```

### GitHub

Make the repository public before the first release; Sparkle downloads release
assets without authentication.

Create a `release` GitHub environment and populate it with the secrets and
variables listed under [Release secrets](DEVELOPMENT.md#release-secrets):
Developer ID certificate, Apple notarization credentials, Sparkle keys, Sentry
token and DSN, and the Cloudflare token and account ID. To require a manual
approval click before either workflow runs, add yourself as a required
reviewer on that environment.

### Sparkle signing key

Resolve the pinned Sparkle package, then use Sparkle's key tool:

```sh
sparkle_bin=$(./scripts/resolve-sparkle-tools.sh)
"${sparkle_bin}/generate_keys" --account com.jjkr.spacehound
"${sparkle_bin}/generate_keys" --account com.jjkr.spacehound \
  -x /secure/offline/location/spacehound-sparkle-private-key
```

The first command prints `SPARKLE_PUBLIC_ED_KEY`. The second exports the
private seed already stored in Keychain; its target file must not exist
beforehand. Store that seed as the `release` environment secret
`SPARKLE_ED_PRIVATE_KEY` and in an encrypted offline backup, then remove every
unencrypted temporary copy.

### Sentry

The `jjkr/spacehound` Sentry project must have default data scrubbing enabled
and **Prevent Storing of IP Addresses** turned on under Security & Privacy. The
`SENTRY_AUTH_TOKEN` needs `org:ci` access.

## 1. Prepare the release

Copy the release-note template to a file named for the version, write the
user-facing notes under second-level headings, and remove the
`RELEASE_NOTES_PLACEHOLDER` comment. Do not add a top-level heading; Sparkle
and GitHub add the title.

```sh
cp release-notes/TEMPLATE.md release-notes/v0.4.0.md
```

Commit the notes with the release changes so they are reviewed and the tagged
commit permanently records what was published. Validate locally:

```sh
make release-script-tests
./scripts/prepare-release-notes.sh 0.4.0 release-notes/v0.4.0.md build/notes-preview.md
```

Confirm the commit is on `main`, the branch is synchronized with GitHub, and
the working tree is clean:

```sh
git switch main
git pull --ff-only
git status --short
```

## 2. Release to beta

```sh
git tag v0.4.0
git push origin v0.4.0
```

Watch the run:

```sh
gh run list --repo jjkr/spacehound --workflow release.yml --limit 3
gh run watch --repo jjkr/spacehound --interval 10 --exit-status
```

A successful run publishes:

- GitHub pre-release `v0.4.0` with `SpaceHound-0.4.0-arm64.zip`,
  `SpaceHound-0.4.0-arm64.dmg`, and `SpaceHound-0.4.0-SHA256SUMS.txt`.
- The feed with a new `0.4.0` item on the `beta` channel, deployed to
  `https://updates.spacehound.app/appcast.xml`.
- The archive's dSYMs to Sentry. Missing credentials, missing symbols, or a
  failed upload stop the release before anything is published.

## 3. Verify the beta

Do not promote until the beta has been approved. At minimum:

- Confirm the feed names the version on the beta channel:

  ```sh
  curl --fail --show-error https://updates.spacehound.app/appcast.xml \
    | grep -E 'sparkle:(version|channel)'
  ```

- In an installed copy of SpaceHound, enable **Receive Beta Updates**, choose
  **Check for Updates…**, and install the beta.
- Confirm the update signature is accepted, the release notes render,
  installation completes, the app relaunches, and its core behavior works.
- With **Receive Beta Updates** disabled in another installation, confirm
  **Check for Updates…** does not offer it.
- For the first monitored release, use a disposable pre-release build with a
  temporary intentional crash, relaunch it to send the cached event, and confirm
  Sentry shows the expected release with symbolicated SpaceHound frames.

## 4. Promote to production

```sh
gh release edit v0.4.0 --repo jjkr/spacehound --prerelease=false --latest
```

This is the release gate: anyone who can edit releases can promote. The
**Promote** workflow starts automatically. Watch it:

```sh
gh run list --repo jjkr/spacehound --workflow promote.yml --limit 3
gh run watch --repo jjkr/spacehound --interval 10 --exit-status
```

Promotion fetches the published feed, downloads the archive from the GitHub
release, verifies it against the feed's EdDSA signature, Developer ID
signature, notarization ticket, bundle identifier, versions, feed URL, and
public key, removes the beta channel tag from that item, drops superseded beta
items, re-signs the feed, and deploys it.

## 5. Verify production

```sh
curl --fail --show-error https://updates.spacehound.app/appcast.xml \
  | grep -E 'sparkle:(version|channel)'
gh release view v0.4.0 --repo jjkr/spacehound
gh release view --repo jjkr/spacehound   # the latest (non-pre-release) release
```

Also disable **Receive Beta Updates** in an installation, choose **Check for
Updates…**, and confirm it sees and installs the new production release.

## Corrections and recovery

| Situation | Action |
| --- | --- |
| Release fails before the GitHub pre-release exists | Fix the cause, then delete and re-push the tag. |
| Release fails after the pre-release exists but before the feed deploys | The version is invisible to Sparkle. Delete the pre-release and the tag (`gh release delete v0.4.0 --cleanup-tag`), fix the cause, and push the tag again. |
| Beta is in the feed but needs a fix | Leave it as a pre-release and release the next version. Never delete a release that has appeared in the feed. |
| Promote fails before the feed deploys | Nothing changed for users. Fix the cause and re-run it: **Actions → Promote → Run workflow** with the tag. |
| Promote ran but the feed deploy must be redone | Re-run **Promote** manually with the tag. It refuses to run twice for the same version once the feed already shows it on the production channel; in that case redeploy from the run's `appcast-vX.Y.Z-production` artifact with `make updates-deploy`. |
| A promoted release is bad | Sparkle never downgrades. Stop the rollout by rolling back the Worker in the Cloudflare dashboard (Workers → spacehound-updates → Deployments), then release and promote a fix. |

Both workflows keep the feed they deployed as a run artifact for 30 days.

## Key-handling rules

- Never print, commit, or put the Sparkle private key in a command argument.
  The scripts read it from standard input only.
- Keep all release secrets scoped to the `release` environment, not the
  repository.
- Rotate or recover a signing key only through a separately planned release;
  losing the current private key prevents normal signed updates.
