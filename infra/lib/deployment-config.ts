export interface DeliveryEnvironmentConfig {
  readonly account: string;
  readonly domainName: string;
  readonly githubEnvironment: string;
  readonly name: "beta" | "production";
}

export interface DeploymentConfig {
  readonly beta: DeliveryEnvironmentConfig;
  readonly githubBranch: string;
  readonly githubConnectionArn: string;
  readonly githubRepository: string;
  readonly infraAccount: string;
  readonly parentHostedZoneId: string;
  readonly production: DeliveryEnvironmentConfig;
  readonly region: string;
  readonly rootDomainName: string;
}

interface ContextReader {
  node: {
    tryGetContext(key: string): unknown;
  };
}

function requiredContext(app: ContextReader, key: string): string {
  const value = app.node.tryGetContext(key);
  if (typeof value !== "string" || value.length === 0) {
    throw new Error(`Pass required CDK context -c ${key}=VALUE`);
  }
  return value;
}

export function deploymentConfig(app: ContextReader): DeploymentConfig {
  return {
    beta: {
      account: requiredContext(app, "betaAccount"),
      domainName: requiredContext(app, "betaDomainName"),
      githubEnvironment: requiredContext(app, "betaGitHubEnvironment"),
      name: "beta",
    },
    githubBranch: requiredContext(app, "githubBranch"),
    githubConnectionArn: requiredContext(app, "githubConnectionArn"),
    githubRepository: requiredContext(app, "githubRepository"),
    infraAccount: requiredContext(app, "infraAccount"),
    parentHostedZoneId: requiredContext(app, "parentHostedZoneId"),
    production: {
      account: requiredContext(app, "prodAccount"),
      domainName: requiredContext(app, "prodDomainName"),
      githubEnvironment: requiredContext(app, "prodGitHubEnvironment"),
      name: "production",
    },
    region: "us-east-1",
    rootDomainName: requiredContext(app, "rootDomainName"),
  };
}
