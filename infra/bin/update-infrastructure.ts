#!/usr/bin/env node

import { App } from "aws-cdk-lib";
import { deploymentConfig } from "../lib/deployment-config.js";
import { PipelineStack } from "../lib/pipeline-stack.js";

const app = new App();
const config = deploymentConfig(app);

new PipelineStack(app, "SpaceHoundInfrastructurePipeline", {
  config,
  env: { account: config.infraAccount, region: config.region },
  stackName: "SpaceHound-InfrastructurePipeline",
});
