import { App } from "aws-cdk-lib";
import { Match, Template } from "aws-cdk-lib/assertions";
import { describe, it } from "vitest";
import { PipelineStack } from "../lib/pipeline-stack.js";

describe("SpaceRabbit infrastructure pipeline", () => {
  const app = new App();
  const stack = new PipelineStack(app, "Pipeline", {
    config: {
      beta: {
        account: "499246566000",
        domainName: "beta-updates.getspacerabbit.com",
        githubEnvironment: "beta",
        githubOidcProviderArn:
          "arn:aws:iam::499246566000:oidc-provider/token.actions.githubusercontent.com",
        name: "beta",
      },
      githubBranch: "main",
      githubConnectionArn:
        "arn:aws:codestar-connections:us-east-1:155091848123:connection/00000000-0000-0000-0000-000000000000",
      githubRepository: "animaslabs/spacerabbit",
      infraAccount: "155091848123",
      parentHostedZoneId: "Z0123456789EXAMPLE",
      production: {
        account: "772699011759",
        domainName: "updates.getspacerabbit.com",
        githubEnvironment: "production",
        githubOidcProviderArn:
          "arn:aws:iam::772699011759:oidc-provider/token.actions.githubusercontent.com",
        name: "production",
      },
      region: "us-east-1",
      rootDomainName: "getspacerabbit.com",
    },
    env: { account: "155091848123", region: "us-east-1" },
  });
  const template = Template.fromStack(stack);

  it("creates a V2 self-mutating cross-account pipeline", () => {
    template.hasResourceProperties("AWS::CodePipeline::Pipeline", {
      PipelineType: "V2",
      Stages: Match.arrayWith([
        Match.objectLike({ Name: "Source" }),
        Match.objectLike({ Name: "Build" }),
        Match.objectLike({ Name: "UpdatePipeline" }),
        Match.objectLike({ Name: "Beta" }),
        Match.objectLike({ Name: "Production" }),
      ]),
    });
  });

  it("keeps production behind manual approval", () => {
    template.hasResourceProperties("AWS::CodePipeline::Pipeline", {
      Stages: Match.arrayWith([
        Match.objectLike({
          Name: "Production",
          Actions: Match.arrayWith([
            Match.objectLike({
              ActionTypeId: Match.objectLike({ Provider: "Manual" }),
            }),
          ]),
        }),
      ]),
    });
  });
});
