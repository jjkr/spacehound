import { App } from "aws-cdk-lib";
import { Match, Template } from "aws-cdk-lib/assertions";
import { describe, expect, it } from "vitest";
import { UpdateDistributionStack } from "../lib/update-distribution-stack.js";

function template(): Template {
  const app = new App();
  const stack = new UpdateDistributionStack(app, "TestStack", {
    env: { account: "123456789012", region: "us-east-1" },
    domainName: "updates.spacerabbit.io",
    githubRepository: "animaslabs/spacerabbit",
    hostedZoneId: "Z0123456789EXAMPLE",
    hostedZoneName: "spacerabbit.io",
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

  it("serves the signed feed and artifacts through CloudFront", () => {
    template().hasResourceProperties("AWS::CloudFront::Distribution", {
      DistributionConfig: Match.objectLike({
        Aliases: ["updates.spacerabbit.io"],
        CacheBehaviors: Match.arrayWith([
          Match.objectLike({ PathPattern: "appcast.xml" }),
          Match.objectLike({ PathPattern: "staging/appcast.xml" }),
        ]),
        Enabled: true,
        HttpVersion: "http2and3",
        Origins: Match.arrayWith([
          Match.objectLike({ OriginAccessControlId: Match.anyValue() }),
        ]),
      }),
    });
  });

  it("restricts GitHub OIDC trust to the protected production environment", () => {
    template().hasResourceProperties("AWS::IAM::Role", {
      AssumeRolePolicyDocument: Match.objectLike({
        Statement: Match.arrayWith([
          Match.objectLike({
            Condition: {
              StringEquals: {
                "token.actions.githubusercontent.com:aud": "sts.amazonaws.com",
                "token.actions.githubusercontent.com:sub":
                  "repo:animaslabs/spacerabbit:environment:production",
              },
            },
          }),
        ]),
      }),
    });
  });
});
