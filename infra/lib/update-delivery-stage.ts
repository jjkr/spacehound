import { Stage, type StageProps } from "aws-cdk-lib";
import type { Construct } from "constructs";
import type { DeliveryEnvironmentConfig } from "./deployment-config.js";
import { UpdateDistributionStack } from "./update-distribution-stack.js";

export interface UpdateDeliveryStageProps extends StageProps {
  readonly delegationRoleArn: string;
  readonly environmentConfig: DeliveryEnvironmentConfig;
  readonly githubRepository: string;
  readonly parentHostedZoneId: string;
}

export class UpdateDeliveryStage extends Stage {
  public constructor(scope: Construct, id: string, props: UpdateDeliveryStageProps) {
    super(scope, id, props);

    new UpdateDistributionStack(this, "UpdateDistribution", {
      delegationRoleArn: props.delegationRoleArn,
      domainName: props.environmentConfig.domainName,
      environmentName: props.environmentConfig.name,
      env: props.env,
      githubEnvironment: props.environmentConfig.githubEnvironment,
      githubOidcProviderArn: props.environmentConfig.githubOidcProviderArn,
      githubRepository: props.githubRepository,
      parentHostedZoneId: props.parentHostedZoneId,
      stackName: `SpaceRabbit-${props.environmentConfig.name}-UpdateDistribution`,
    });
  }
}
