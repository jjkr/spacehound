#!/usr/bin/env node

import { App } from "aws-cdk-lib";
import { UpdateDistributionStack } from "../lib/update-distribution-stack.js";

const app = new App();
const hostedZoneId = app.node.tryGetContext("hostedZoneId") as string | undefined;

if (!hostedZoneId) {
  throw new Error(
    "Pass the manually created Route 53 zone ID with -c hostedZoneId=ZXXXXXXXXXXXXX",
  );
}

new UpdateDistributionStack(app, "SpaceRabbitUpdateDistribution", {
  env: {
    account: process.env.CDK_DEFAULT_ACCOUNT,
    region: process.env.CDK_DEFAULT_REGION ?? "us-east-1",
  },
  crossRegionReferences: false,
  domainName: app.node.tryGetContext("domainName") as string,
  githubRepository: app.node.tryGetContext("githubRepository") as string,
  hostedZoneId,
  hostedZoneName: app.node.tryGetContext("hostedZoneName") as string,
  githubOidcProviderArn: app.node.tryGetContext("githubOidcProviderArn") as string | undefined,
});
