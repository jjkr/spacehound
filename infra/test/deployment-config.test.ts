import { describe, expect, it } from "vitest";
import { deploymentConfig } from "../lib/deployment-config.js";

function context(overrides: Record<string, unknown> = {}) {
  const values: Record<string, unknown> = {
    betaAccount: "499246566000",
    betaBandwidthAlarmGibPerHour: 1,
    betaDomainName: "beta-updates.getspacerabbit.com",
    betaGitHubEnvironment: "beta",
    githubBranch: "main",
    githubConnectionArn: "arn:aws:codeconnections:us-east-1:123456789012:connection/test",
    githubRepository: "animaslabs/spacerabbit",
    infraAccount: "155091848123",
    parentHostedZoneId: "Z0123456789EXAMPLE",
    prodAccount: "772699011759",
    prodBandwidthAlarmGibPerHour: 10,
    prodDomainName: "updates.getspacerabbit.com",
    prodGitHubEnvironment: "production",
    rootDomainName: "getspacerabbit.com",
    ...overrides,
  };
  return {
    node: {
      tryGetContext(key: string): unknown {
        return values[key];
      },
    },
  };
}

describe("deployment configuration", () => {
  it("loads independent bandwidth limits for beta and production", () => {
    const config = deploymentConfig(context());
    expect(config.beta.bandwidthAlarmGibPerHour).toBe(1);
    expect(config.production.bandwidthAlarmGibPerHour).toBe(10);
  });

  it("accepts numeric bandwidth limits passed on the CDK command line", () => {
    const config = deploymentConfig(context({
      betaBandwidthAlarmGibPerHour: "2.5",
    }));
    expect(config.beta.bandwidthAlarmGibPerHour).toBe(2.5);
  });

  it.each([0, -1, "not-a-number", undefined])(
    "rejects invalid bandwidth limit %s",
    (value) => {
      expect(() => deploymentConfig(context({
        betaBandwidthAlarmGibPerHour: value,
      }))).toThrow("betaBandwidthAlarmGibPerHour");
    },
  );
});
