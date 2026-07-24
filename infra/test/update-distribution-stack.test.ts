import { App } from "aws-cdk-lib";
import { Match, Template } from "aws-cdk-lib/assertions";
import { describe, expect, it } from "vitest";
import { UpdateDistributionStack } from "../lib/update-distribution-stack.js";

function template(
  environmentName: "beta" | "production" = "production",
  githubOidcProviderArn?: string,
): Template {
  const app = new App();
  const domainName = environmentName === "beta"
    ? "beta-updates.getspacehound.com"
    : "updates.getspacehound.com";
  const stack = new UpdateDistributionStack(app, "TestStack", {
    bandwidthAlarmGibPerHour: environmentName === "beta" ? 1 : 10,
    delegationRoleArn: "arn:aws:iam::155091848123:role/SpaceHoundDnsDelegationRole",
    domainName,
    env: { account: "123456789012", region: "us-east-1" },
    environmentName,
    githubEnvironment: environmentName === "beta" ? "beta" : "production",
    githubOidcProviderArn,
    githubRepository: "jjkr/spacehound",
    parentHostedZoneId: "Z0123456789EXAMPLE",
  });
  return Template.fromStack(stack);
}

describe("SpaceHound update distribution", () => {
  it("retains a private, encrypted, versioned artifact bucket", () => {
    template().hasResource("AWS::S3::Bucket", {
      DeletionPolicy: "Retain",
      Properties: {
        BucketEncryption: Match.anyValue(),
        PublicAccessBlockConfiguration: {
          BlockPublicAcls: true,
          BlockPublicPolicy: true,
          IgnorePublicAcls: true,
          RestrictPublicBuckets: true,
        },
        VersioningConfiguration: { Status: "Enabled" },
      },
      UpdateReplacePolicy: "Retain",
    });
  });

  it("serves one environment through an independent domain and bucket", () => {
    template("beta").hasResourceProperties("AWS::CloudFront::Distribution", {
      DistributionConfig: Match.objectLike({
        Aliases: ["beta-updates.getspacehound.com"],
        CacheBehaviors: Match.arrayWith([
          Match.objectLike({ PathPattern: "appcast.xml" }),
          Match.objectLike({ PathPattern: "releases/latest/*" }),
        ]),
        Enabled: true,
        HttpVersion: "http2and3",
        Origins: Match.arrayWith([
          Match.objectLike({ OriginAccessControlId: Match.anyValue() }),
        ]),
      }),
    });
  });

  it("publishes additional CloudFront metrics and validates public endpoints", () => {
    const betaTemplate = template("beta");
    betaTemplate.hasResourceProperties("AWS::CloudFront::MonitoringSubscription", {
      DistributionId: Match.anyValue(),
      MonitoringSubscription: {
        RealtimeMetricsSubscriptionConfig: { RealtimeMetricsSubscriptionStatus: "Enabled" },
      },
    });
    betaTemplate.hasResourceProperties("AWS::Synthetics::Canary", {
      Name: "sh-beta-updates",
      RunConfig: Match.objectLike({
        EnvironmentVariables: {
          BASE_URL: "https://beta-updates.getspacehound.com",
        },
      }),
      RuntimeVersion: "syn-nodejs-puppeteer-12.0",
      Schedule: { Expression: "rate(5 minutes)" },
      StartCanaryAfterCreation: true,
    });
  });

  it("creates account-local dashboards and low-noise alarms without actions", () => {
    const betaTemplate = template("beta");
    betaTemplate.hasResourceProperties("AWS::CloudWatch::Dashboard", {
      DashboardName: "SpaceHound-beta-UpdateDelivery",
    });
    betaTemplate.hasResourceProperties("AWS::CloudWatch::Alarm", {
      AlarmName: "SpaceHound-beta-UpdateEndpoint",
      ComparisonOperator: "LessThanThreshold",
      DatapointsToAlarm: 2,
      EvaluationPeriods: 3,
      Threshold: 100,
      TreatMissingData: "breaching",
    });
    betaTemplate.hasResourceProperties("AWS::CloudWatch::Alarm", {
      AlarmName: "SpaceHound-beta-HourlyBandwidth",
      ComparisonOperator: "GreaterThanThreshold",
      EvaluationPeriods: 1,
      Metrics: Match.arrayWith([
        Match.objectLike({ MetricStat: Match.objectLike({ Period: 3600 }) }),
      ]),
      Threshold: 1073741824,
      TreatMissingData: "notBreaching",
    });
    for (const [status, threshold] of [["4xx", 10], ["5xx", 5]] as const) {
      betaTemplate.hasResourceProperties("AWS::CloudWatch::Alarm", {
        AlarmName: `SpaceHound-beta-CloudFront${status}`,
        ComparisonOperator: "GreaterThanThreshold",
        DatapointsToAlarm: 2,
        EvaluationPeriods: 3,
        Metrics: Match.arrayWith([
          Match.objectLike({ Expression: "IF(requests >= 20, errors, 0)" }),
        ]),
        Threshold: threshold,
        TreatMissingData: "notBreaching",
      });
    }
    betaTemplate.hasResourceProperties("AWS::CloudWatch::Alarm", {
      AlarmName: "SpaceHound-beta-CertificateExpiry",
      ComparisonOperator: "LessThanThreshold",
      EvaluationPeriods: 1,
      Threshold: 30,
      TreatMissingData: "notBreaching",
    });

    const rendered = betaTemplate.toJSON();
    const alarms = Object.values(rendered.Resources)
      .filter((resource: any) => resource.Type === "AWS::CloudWatch::Alarm");
    expect(alarms).toHaveLength(5);
    expect(alarms.every((resource: any) =>
      resource.Properties.AlarmActions === undefined &&
      resource.Properties.InsufficientDataActions === undefined &&
      resource.Properties.OKActions === undefined
    )).toBe(true);
    expect(betaTemplate.findResources("AWS::SNS::Topic")).toEqual({});
  });

  it("uses the production bandwidth threshold independently", () => {
    template("production").hasResourceProperties("AWS::CloudWatch::Alarm", {
      AlarmName: "SpaceHound-production-HourlyBandwidth",
      Threshold: 10737418240,
    });
  });

  it("delegates the child zone through the infra-account role", () => {
    template().hasResource("AWS::Route53::HostedZone", {
      DeletionPolicy: "Retain",
      UpdateReplacePolicy: "Retain",
    });
    template().hasResourceProperties("Custom::CrossAccountZoneDelegation", {
      AssumeRoleArn: "arn:aws:iam::155091848123:role/SpaceHoundDnsDelegationRole",
      DelegatedZoneName: "updates.getspacehound.com",
      ParentZoneId: "Z0123456789EXAMPLE",
    });
  });

  it("restricts GitHub OIDC trust to the matching GitHub environment", () => {
    template("beta").hasResourceProperties("AWS::IAM::Role", {
      AssumeRolePolicyDocument: Match.objectLike({
        Statement: Match.arrayWith([
          Match.objectLike({
            Condition: {
              StringEquals: {
                "token.actions.githubusercontent.com:aud": "sts.amazonaws.com",
                "token.actions.githubusercontent.com:sub":
                  "repo:jjkr/spacehound:environment:beta",
              },
            },
          }),
        ]),
      }),
    });
  });

  it("imports an existing account-level GitHub OIDC provider when configured", () => {
    const providerArn =
      "arn:aws:iam::123456789012:oidc-provider/token.actions.githubusercontent.com";
    const importedTemplate = template("production", providerArn);

    expect(importedTemplate.findResources("Custom::AWSCDKOpenIdConnectProvider")).toEqual({});
    importedTemplate.hasResourceProperties("AWS::IAM::Role", {
      AssumeRolePolicyDocument: Match.objectLike({
        Statement: Match.arrayWith([
          Match.objectLike({
            Principal: { Federated: providerArn },
          }),
        ]),
      }),
    });
  });

  it("limits the publisher to appcast and release object paths", () => {
    const rendered = template().toJSON();
    const policies = Object.values(rendered.Resources)
      .filter((resource: any) => resource.Type === "AWS::IAM::Policy")
      .map((resource: any) => JSON.stringify(resource.Properties.PolicyDocument));
    expect(policies.some((policy) => policy.includes("releases/*"))).toBe(true);
    expect(policies.every((policy) => !policy.includes("staging/"))).toBe(true);
  });
});
