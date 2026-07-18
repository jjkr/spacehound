export interface DeliveryEnvironmentConfig {
  readonly account: string;
  readonly domainName: string;
  readonly githubEnvironment: string;
  readonly githubOidcProviderArn?: string;
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

function optionalContext(app: ContextReader, key: string): string | undefined {
  const value = app.node.tryGetContext(key);
  if (value === undefined || value === "") {
    return undefined;
  }
  if (typeof value !== "string") {
    throw new Error(`CDK context ${key} must be a string`);
  }
  return value;
}

export function deploymentConfig(app: ContextReader): DeploymentConfig {
  return {
    beta: {
      account: requiredContext(app, "betaAccount"),
      domainName: requiredContext(app, "betaDomainName"),
      githubEnvironment: requiredContext(app, "betaGitHubEnvironment"),
      githubOidcProviderArn: optionalContext(app, "betaGitHubOidcProviderArn"),
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
      githubOidcProviderArn: optionalContext(app, "prodGitHubOidcProviderArn"),
      name: "production",
    },
    region: "us-east-1",
    rootDomainName: requiredContext(app, "rootDomainName"),
  };
}
