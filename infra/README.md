# SpaceRabbit update infrastructure

This CDK app provisions the private S3 origin, CloudFront distribution,
certificate, DNS records, and least-privilege GitHub Actions publisher role for
`updates.spacerabbit.io`. It does not own or register the apex domain.

## One-time DNS setup

1. Create a public Route 53 hosted zone named `spacerabbit.io` manually.
2. At Porkbun, replace the domain's authoritative name servers with the four
   Route 53 name servers.
3. Wait until `dig NS spacerabbit.io` returns the Route 53 delegation.
4. Record the hosted zone ID. CDK consumes the existing zone and will create
   only the certificate validation and `updates.spacerabbit.io` alias records.

## Install and deploy

The repository pins Node and pnpm through mise; do not install or invoke npm
directly.

```sh
mise install
mise exec -- pnpm --dir infra install --frozen-lockfile
mise exec -- pnpm --dir infra run build
mise exec -- pnpm --dir infra test

mise exec -- pnpm --dir infra exec cdk bootstrap aws://AWS_ACCOUNT_ID/us-east-1
mise exec -- pnpm --dir infra exec cdk deploy \
  -c hostedZoneId=ROUTE53_HOSTED_ZONE_ID
```

If the AWS account already has a GitHub Actions OIDC provider, import it instead
of creating a duplicate:

```sh
mise exec -- pnpm --dir infra exec cdk deploy \
  -c hostedZoneId=ROUTE53_HOSTED_ZONE_ID \
  -c githubOidcProviderArn=arn:aws:iam::AWS_ACCOUNT_ID:oidc-provider/token.actions.githubusercontent.com
```

Deploy the initial stack from a trusted workstation. Copy its bucket,
distribution, and publisher-role outputs into the protected `production`
GitHub environment using the variable names documented in `DEVELOPMENT.md`.

The bucket and its version history are retained if the stack is deleted. The
CloudFront origin uses Origin Access Control; the S3 bucket is never public.
Versioned release objects cache for one year and are immutable. Appcasts and
`releases/latest` aliases are uploaded with `no-cache` and explicitly
invalidated after publication.

## Sparkle signing key bootstrap

Resolve the pinned Sparkle package, then use Sparkle's own key tool with a
SpaceRabbit-specific account name:

```sh
make generate
xcodebuild -resolvePackageDependencies \
  -project SpaceRabbit.xcodeproj \
  -scheme SpaceRabbit \
  -derivedDataPath build/DerivedData

sparkle_bin=build/DerivedData/SourcePackages/artifacts/sparkle/Sparkle/bin
"${sparkle_bin}/generate_keys" --account com.animaslabs.SpaceRabbit
"${sparkle_bin}/generate_keys" --account com.animaslabs.SpaceRabbit \
  -x /secure/offline/location/spacerabbit-sparkle-private-key
```

The first command prints the public key for `SPARKLE_PUBLIC_ED_KEY`. Store the
exported private seed in the GitHub secret `SPARKLE_ED_PRIVATE_KEY` and in an
encrypted offline backup, then remove any unencrypted temporary copy.

## Staging verification

The manually dispatched release workflow builds with
`https://updates.spacerabbit.io/staging/appcast.xml`. Staging objects live under
`staging/releases/vX.Y.Z/`, are not mirrored to GitHub Releases, and cannot
modify the production feed. Test a notarized `0.1.0` staging install upgrading
to `0.1.1`, including relaunch and preserved settings, before the first
production release.
