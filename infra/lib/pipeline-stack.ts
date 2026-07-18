import { Stack, type StackProps } from "aws-cdk-lib";
import * as codepipeline from "aws-cdk-lib/aws-codepipeline";
import * as iam from "aws-cdk-lib/aws-iam";
import { CodePipeline, CodePipelineSource, ManualApprovalStep, ShellStep } from "aws-cdk-lib/pipelines";
import * as route53 from "aws-cdk-lib/aws-route53";
import type { Construct } from "constructs";
import type { DeploymentConfig } from "./deployment-config.js";
import { UpdateDeliveryStage } from "./update-delivery-stage.js";

export interface PipelineStackProps extends StackProps {
  readonly config: DeploymentConfig;
}

export class PipelineStack extends Stack {
  public constructor(scope: Construct, id: string, props: PipelineStackProps) {
    super(scope, id, props);

    const parentZone = route53.PublicHostedZone.fromHostedZoneAttributes(this, "RootZone", {
      hostedZoneId: props.config.parentHostedZoneId,
      zoneName: props.config.rootDomainName,
    });
    const delegationRole = new iam.Role(this, "DnsDelegationRole", {
      assumedBy: new iam.CompositePrincipal(
        new iam.AccountPrincipal(props.config.beta.account),
        new iam.AccountPrincipal(props.config.production.account),
      ),
      roleName: "SpaceRabbitDnsDelegationRole",
    });
    parentZone.grantDelegation(delegationRole, {
      delegatedZoneNames: [
        props.config.beta.domainName,
        props.config.production.domainName,
      ],
    });
    const delegationRoleArn = `arn:${this.partition}:iam::${props.config.infraAccount}:role/SpaceRabbitDnsDelegationRole`;

    const source = CodePipelineSource.connection(
      props.config.githubRepository,
      props.config.githubBranch,
      {
        connectionArn: props.config.githubConnectionArn,
        triggerOnPush: false,
      },
    );
    const pipeline = new CodePipeline(this, "DeliveryPipeline", {
      cdkAssetsCliVersion: "2.1132.0",
      cliVersion: "2.1132.0",
      crossAccountKeys: true,
      pipelineName: "SpaceRabbit-Infrastructure",
      pipelineType: codepipeline.PipelineType.V2,
      selfMutation: true,
      synth: new ShellStep("Synth", {
        input: source,
        installCommands: [
          "curl https://mise.run | sh",
          "$HOME/.local/bin/mise install",
          "$HOME/.local/bin/mise exec -- npm --prefix infra ci",
        ],
        commands: [
          "$HOME/.local/bin/mise exec -- npm --prefix infra run build",
          "$HOME/.local/bin/mise exec -- npm --prefix infra test",
          "$HOME/.local/bin/mise exec -- npm --prefix infra run synth -- --quiet",
        ],
        primaryOutputDirectory: "infra/cdk.out",
      }),
    });

    const beta = new UpdateDeliveryStage(this, "Beta", {
      delegationRoleArn,
      env: { account: props.config.beta.account, region: props.config.region },
      environmentConfig: props.config.beta,
      githubRepository: props.config.githubRepository,
      parentHostedZoneId: props.config.parentHostedZoneId,
    });
    pipeline.addStage(beta, {
      post: [new ShellStep("VerifyBetaEndpoint", {
        commands: [
          `response=$(curl --silent --show-error --output /dev/null --write-out '%{http_code}' https://${props.config.beta.domainName}/appcast.xml)`,
          "test \"$response\" = 200 -o \"$response\" = 403 -o \"$response\" = 404",
        ],
      })],
    });

    const production = new UpdateDeliveryStage(this, "Production", {
      delegationRoleArn,
      env: { account: props.config.production.account, region: props.config.region },
      environmentConfig: props.config.production,
      githubRepository: props.config.githubRepository,
      parentHostedZoneId: props.config.parentHostedZoneId,
    });
    pipeline.addStage(production, {
      pre: [new ManualApprovalStep("ApproveProduction", {
        comment: "Verify the beta update distribution before deploying production",
        reviewUrl: `https://${props.config.beta.domainName}/appcast.xml`,
      })],
    });

    pipeline.buildPipeline();
    const sourceAction = pipeline.pipeline.stage("Source").actions[0];
    if (sourceAction === undefined) {
      throw new Error("CDK Pipelines did not create its source action");
    }
    pipeline.pipeline.addTrigger({
      providerType: codepipeline.ProviderType.CODE_STAR_SOURCE_CONNECTION,
      gitConfiguration: {
        sourceAction,
        pushFilter: [{
          branchesIncludes: [props.config.githubBranch],
          filePathsIncludes: ["infra/**", "mise.toml"],
        }],
      },
    });
  }
}
