# SpaceHound update infrastructure

The CDK app creates a self-mutating CDK Pipeline in the infrastructure account
and deploys independent update-delivery stacks to beta and production:

| Purpose | Account | Domain |
| --- | --- | --- |
| Pipeline and root DNS | `spacehound-infra` (`155091848123`) | `spacehound.app` |
| Beta updates | `spacehound-beta` (`499246566000`) | `beta-updates.spacehound.app` |
| Production updates | `spacehound-prod` (`772699011759`) | `updates.spacehound.app` |

The pipeline automatically deploys beta, verifies its endpoint, then waits for
manual approval before deploying production. It only triggers for changes to
`infra/**` or `mise.toml` on `main`. The existing `spacehound.app` website is
unrelated and is not changed by these stacks.

## Root DNS and GitHub connection

1. In the infra account, use the authoritative public Route 53 hosted zone for
   `spacehound.app`, or create one and copy every existing website DNS record
   into it before changing the domain's name servers. Record its hosted-zone ID.
2. If a new hosted zone was created, replace the domain's authoritative name
   servers at the registrar only after verifying that it contains the existing
   website records.
3. In **Developer Tools > Connections** in `us-east-1`, create a GitHub
   connection for `jjkr/spacehound` and complete the pending GitHub
   authorization. Record the connection ARN.
4. Put both non-secret values into `infra/cdk.json` as
   `parentHostedZoneId` and `githubConnectionArn`, replacing the empty
   placeholders, and commit them. They must be committed because the pipeline's
   future self-mutation runs synth from the repository.
5. If a workload account already has the GitHub Actions OIDC provider, set that
   account's `betaGitHubOidcProviderArn` or `prodGitHubOidcProviderArn` value in
   `infra/cdk.json`. For example, the production ARN is
   `arn:aws:iam::772699011759:oidc-provider/token.actions.githubusercontent.com`.
   Leave the value empty when the stack should create the provider.

An imported provider remains owned by whatever originally created it. Keep that
resource in place; do not delete its old owning stack until the provider has
been migrated to durable account-level ownership.

Each workload stack creates its own child hosted zone. A narrowly scoped role in
the infra account lets the workload accounts write only the NS delegation for
their child domain into the root zone. Certificates are validated inside the
child zones. A separate DNS account is unnecessary at this scale.

## Install and validate

Node and npm are pinned by mise; npm comes with the pinned Node installation.
Run npm through mise so local and pipeline versions agree:

```sh
mise install
mise exec -- npm --prefix infra ci
mise exec -- npm --prefix infra run build
mise exec -- npm --prefix infra test
```

## Bootstrap the three accounts

CDK Pipelines needs modern bootstrap stacks in all three accounts. Use local AWS
profiles that can administer their corresponding accounts:

```sh
AWS_PROFILE=spacehound-infra mise exec -- npm --prefix infra run cdk -- \
  bootstrap aws://155091848123/us-east-1

AWS_PROFILE=spacehound-beta mise exec -- npm --prefix infra run cdk -- \
  bootstrap aws://499246566000/us-east-1 \
  --trust 155091848123 \
  --cloudformation-execution-policies arn:aws:iam::aws:policy/AdministratorAccess

AWS_PROFILE=spacehound-prod mise exec -- npm --prefix infra run cdk -- \
  bootstrap aws://772699011759/us-east-1 \
  --trust 155091848123 \
  --cloudformation-execution-policies arn:aws:iam::aws:policy/AdministratorAccess
```

AdministratorAccess is the standard initial CDK execution policy. It applies to
CloudFormation's bootstrap execution role, not to GitHub Actions. It can be
replaced later with a tested narrower policy that covers IAM, Route 53, ACM,
S3, and CloudFront resources used by these stacks.

## Initial pipeline deployment

After the hosted-zone ID and connection ARN are committed in `cdk.json`, deploy
the pipeline once from a trusted workstation:

```sh
AWS_PROFILE=spacehound-infra mise exec -- npm --prefix infra run cdk -- \
  deploy SpaceHoundInfrastructurePipeline
```

The initial deployment creates the DNS delegation role and CodePipeline. Start
the pipeline once from the CodePipeline console (or push a qualifying commit to
`main`); it then self-mutates, deploys beta, and pauses before its first
production deployment. Inspect the beta stack and endpoint before approving
production. Subsequent qualifying commits to `main` are handled automatically.

## Monitoring

Each beta and production update stack owns an environment-specific CloudWatch
dashboard, five alarms, and a CloudWatch Synthetics canary. The canary runs every
five minutes, validates `appcast.xml` as XML, and uses HEAD requests to verify
the current appcast enclosure and `releases/latest/SpaceHound-arm64.dmg`
without downloading either artifact.

The dashboards are named `SpaceHound-beta-UpdateDelivery` and
`SpaceHound-production-UpdateDelivery`. They show endpoint success and
duration, CloudFront requests, error rates, bytes downloaded, cache-hit rate,
origin latency, ACM certificate lifetime, and S3 storage. Their names and the
canary names are also emitted as stack outputs.

The alarms intentionally have no notification actions. Inspect them directly in
CloudWatch until notification routing is added. They enter ALARM for:

- two failed endpoint checks within three five-minute periods;
- 4xx rates over 10% or 5xx rates over 5% in two of three periods with at least
  20 requests per period;
- more than 1 GiB downloaded by beta or 10 GiB by production in one hour; or
- fewer than 30 days remaining on the environment's ACM certificate.

The endpoint alarm treats missing canary data as a failure, so it can briefly be
INSUFFICIENT_DATA or ALARM while the first canary runs complete after deployment.
The bandwidth limits are explicit CDK context values in `cdk.json`:
`betaBandwidthAlarmGibPerHour` and `prodBandwidthAlarmGibPerHour`. Adjust those
values through the normal infrastructure pipeline after observing real traffic.

Buckets are private, encrypted, versioned, and retained on stack deletion; child
hosted zones and their parent delegations are retained as well. CloudFront uses
Origin Access Control. Versioned artifacts are immutable and long cached;
`appcast.xml` and `releases/latest/*` are published with `no-cache` and
invalidated explicitly.

## GitHub release environments

After both workload stacks exist, copy each stack's outputs into the matching
GitHub environment (`beta` or `production`):

- `ArtifactBucketName` -> `AWS_RELEASE_BUCKET`
- `DistributionId` -> `AWS_CLOUDFRONT_DISTRIBUTION_ID`
- `GitHubPublisherRoleArn` -> `AWS_RELEASE_ROLE_ARN`
- set `AWS_RELEASE_REGION` to `us-east-1`
- set `SPARKLE_PUBLIC_ED_KEY` to the same public key in both environments

The roles trust only the exact repository plus GitHub environment name. Put the
Developer ID certificate, notarization credentials, and
`SPARKLE_ED_PRIVATE_KEY` only in `beta`; production promotes the exact candidate
artifact and needs no signing secret. Manual production promotion is a separate
GitHub Actions workflow restricted to the `jjkr` account.

## Sparkle signing key bootstrap

Resolve the pinned Sparkle package, then use Sparkle's key tool with the existing
SpaceHound account name:

```sh
make generate
xcodebuild -resolvePackageDependencies \
  -project SpaceHound.xcodeproj \
  -scheme SpaceHound \
  -derivedDataPath build/DerivedData

sparkle_bin=build/DerivedData/SourcePackages/artifacts/sparkle/Sparkle/bin
"${sparkle_bin}/generate_keys" --account com.jjkr.spacehound
"${sparkle_bin}/generate_keys" --account com.jjkr.spacehound \
  -x /secure/offline/location/spacehound-sparkle-private-key
```

The first command prints `SPARKLE_PUBLIC_ED_KEY`. The second exports the private
seed already stored in Keychain; its target file must not exist beforehand.
Store that seed as the beta environment secret `SPARKLE_ED_PRIVATE_KEY` and in
an encrypted offline backup, then remove every unencrypted temporary copy.

## Candidate promotion

Dispatch **Release candidate** to build `X.Y.ZfcN` once and publish it at
`https://beta-updates.spacehound.app`. Enable **Receive Beta Updates** from
the app's menu to test it. After testing, `jjkr` dispatches **Promote release**
with the successful candidate run ID and matching version inputs. Promotion
downloads that run's retained artifact, verifies its checksums, Developer ID
signature, notarization ticket, bundle metadata, candidate commit, and
pre-generated signed appcast, then publishes it to
`https://updates.spacehound.app` without invoking Xcode.
