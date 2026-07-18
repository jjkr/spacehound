import { App } from "aws-cdk-lib";
import { Match, Template } from "aws-cdk-lib/assertions";
import { describe, expect, it } from "vitest";
import { UpdateDistributionStack } from "../lib/update-distribution-stack.js";

function template(environmentName: "beta" | "production" = "production"): Template {
  const app = new App();
  const domainName = environmentName === "beta"
    ? "beta-updates.getspacerabbit.com"
    : "updates.getspacerabbit.com";
  const stack = new UpdateDistributionStack(app, "TestStack", {
    delegationRoleArn: "arn:aws:iam::155091848123:role/SpaceRabbitDnsDelegationRole",
    domainName,
    env: { account: "123456789012", region: "us-east-1" },
    environmentName,
    githubEnvironment: environmentName === "beta" ? "beta" : "production",
    githubRepository: "animaslabs/spacerabbit",
    parentHostedZoneId: "Z0123456789EXAMPLE",
  });
  return Template.fromStack(stack);
}

describe("SpaceRabbit update distribution", () => {
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
        Aliases: ["beta-updates.getspacerabbit.com"],
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

  it("delegates the child zone through the infra-account role", () => {
    template().hasResource("AWS::Route53::HostedZone", {
      DeletionPolicy: "Retain",
      UpdateReplacePolicy: "Retain",
    });
    template().hasResourceProperties("Custom::CrossAccountZoneDelegation", {
      AssumeRoleArn: "arn:aws:iam::155091848123:role/SpaceRabbitDnsDelegationRole",
      DelegatedZoneName: "updates.getspacerabbit.com",
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
                  "repo:animaslabs/spacerabbit:environment:beta",
              },
            },
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
