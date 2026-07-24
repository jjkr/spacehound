import {
  CfnOutput,
  Duration,
  RemovalPolicy,
  Stack,
  type StackProps,
} from "aws-cdk-lib";
import * as acm from "aws-cdk-lib/aws-certificatemanager";
import * as cloudfront from "aws-cdk-lib/aws-cloudfront";
import * as origins from "aws-cdk-lib/aws-cloudfront-origins";
import * as iam from "aws-cdk-lib/aws-iam";
import * as route53 from "aws-cdk-lib/aws-route53";
import * as targets from "aws-cdk-lib/aws-route53-targets";
import * as s3 from "aws-cdk-lib/aws-s3";
import type { Construct } from "constructs";
import { UpdateMonitoring } from "./update-monitoring.js";

export interface UpdateDistributionStackProps extends StackProps {
  readonly bandwidthAlarmGibPerHour: number;
  readonly delegationRoleArn: string;
  readonly domainName: string;
  readonly environmentName: "beta" | "production";
  readonly githubEnvironment: string;
  readonly githubOidcProviderArn?: string;
  readonly githubRepository: string;
  readonly parentHostedZoneId: string;
}

export class UpdateDistributionStack extends Stack {
  public constructor(scope: Construct, id: string, props: UpdateDistributionStackProps) {
    super(scope, id, props);

    if (this.region !== "us-east-1") {
      throw new Error("Deploy update distributions in us-east-1 for CloudFront ACM certificates");
    }

    const zone = new route53.PublicHostedZone(this, "HostedZone", {
      zoneName: props.domainName,
    });
    zone.applyRemovalPolicy(RemovalPolicy.RETAIN);
    const delegationRole = iam.Role.fromRoleArn(
      this,
      "DnsDelegationRole",
      props.delegationRoleArn,
    );
    const delegation = new route53.CrossAccountZoneDelegationRecord(this, "Delegation", {
      delegatedZone: zone,
      delegationRole,
      parentHostedZoneId: props.parentHostedZoneId,
      removalPolicy: RemovalPolicy.RETAIN,
      ttl: Duration.hours(1),
    });

    const artifactBucket = new s3.Bucket(this, "ArtifactBucket", {
      blockPublicAccess: s3.BlockPublicAccess.BLOCK_ALL,
      encryption: s3.BucketEncryption.S3_MANAGED,
      enforceSSL: true,
      objectOwnership: s3.ObjectOwnership.BUCKET_OWNER_ENFORCED,
      removalPolicy: RemovalPolicy.RETAIN,
      versioned: true,
    });

    const certificate = new acm.Certificate(this, "Certificate", {
      domainName: props.domainName,
      validation: acm.CertificateValidation.fromDns(zone),
    });
    certificate.node.addDependency(delegation);

    const noCachePolicy = new cloudfront.CachePolicy(this, "MutableFeedCachePolicy", {
      defaultTtl: Duration.seconds(0),
      maxTtl: Duration.minutes(5),
      minTtl: Duration.seconds(0),
      enableAcceptEncodingBrotli: true,
      enableAcceptEncodingGzip: true,
    });

    const origin = origins.S3BucketOrigin.withOriginAccessControl(artifactBucket);
    const defaultBehavior: cloudfront.BehaviorOptions = {
      allowedMethods: cloudfront.AllowedMethods.ALLOW_GET_HEAD,
      cachePolicy: cloudfront.CachePolicy.CACHING_OPTIMIZED,
      compress: true,
      origin,
      viewerProtocolPolicy: cloudfront.ViewerProtocolPolicy.REDIRECT_TO_HTTPS,
    };
    const mutableBehavior: cloudfront.BehaviorOptions = {
      ...defaultBehavior,
      cachePolicy: noCachePolicy,
    };

    const distribution = new cloudfront.Distribution(this, "Distribution", {
      certificate,
      defaultBehavior,
      domainNames: [props.domainName],
      httpVersion: cloudfront.HttpVersion.HTTP2_AND_3,
      minimumProtocolVersion: cloudfront.SecurityPolicyProtocol.TLS_V1_2_2021,
      publishAdditionalMetrics: true,
      additionalBehaviors: {
        "appcast.xml": mutableBehavior,
        "releases/latest/*": mutableBehavior,
      },
    });

    for (const [id, record] of [
      ["AliasA", route53.ARecord],
      ["AliasAAAA", route53.AaaaRecord],
    ] as const) {
      new record(this, id, {
        recordName: props.domainName,
        target: route53.RecordTarget.fromAlias(new targets.CloudFrontTarget(distribution)),
        zone,
      });
    }

    const oidcProvider = props.githubOidcProviderArn
      ? iam.OpenIdConnectProvider.fromOpenIdConnectProviderArn(
          this,
          "GitHubOidcProvider",
          props.githubOidcProviderArn,
        )
      : new iam.OpenIdConnectProvider(this, "GitHubOidcProvider", {
          clientIds: ["sts.amazonaws.com"],
          url: "https://token.actions.githubusercontent.com",
        });
    const publisherRole = new iam.Role(this, "GitHubPublisherRole", {
      assumedBy: new iam.WebIdentityPrincipal(oidcProvider.openIdConnectProviderArn, {
        StringEquals: {
          "token.actions.githubusercontent.com:aud": "sts.amazonaws.com",
          "token.actions.githubusercontent.com:sub":
            `repo:${props.githubRepository}:environment:${props.githubEnvironment}`,
        },
      }),
      description: `Publishes SpaceHound ${props.environmentName} updates from GitHub Actions`,
      maxSessionDuration: Duration.hours(1),
    });

    publisherRole.addToPolicy(new iam.PolicyStatement({
      actions: ["s3:GetBucketLocation"],
      resources: [artifactBucket.bucketArn],
    }));
    publisherRole.addToPolicy(new iam.PolicyStatement({
      actions: ["s3:ListBucket"],
      conditions: { StringLike: { "s3:prefix": ["appcast.xml", "releases/*"] } },
      resources: [artifactBucket.bucketArn],
    }));
    publisherRole.addToPolicy(new iam.PolicyStatement({
      actions: ["s3:AbortMultipartUpload", "s3:GetObject", "s3:PutObject"],
      resources: [
        artifactBucket.arnForObjects("appcast.xml"),
        artifactBucket.arnForObjects("releases/*"),
      ],
    }));
    publisherRole.addToPolicy(new iam.PolicyStatement({
      actions: ["cloudfront:CreateInvalidation"],
      resources: [
        `arn:${this.partition}:cloudfront::${this.account}:distribution/${distribution.distributionId}`,
      ],
    }));

    const monitoring = new UpdateMonitoring(this, "Monitoring", {
      artifactBucket,
      bandwidthAlarmGibPerHour: props.bandwidthAlarmGibPerHour,
      certificate,
      distribution,
      domainName: props.domainName,
      environmentName: props.environmentName,
    });

    new CfnOutput(this, "ArtifactBucketName", { value: artifactBucket.bucketName });
    new CfnOutput(this, "CanaryName", { value: monitoring.canary.canaryName });
    new CfnOutput(this, "DashboardName", { value: monitoring.dashboard.dashboardName });
    new CfnOutput(this, "DistributionId", { value: distribution.distributionId });
    new CfnOutput(this, "FeedUrl", { value: `https://${props.domainName}/appcast.xml` });
    new CfnOutput(this, "GitHubPublisherRoleArn", { value: publisherRole.roleArn });
  }
}
