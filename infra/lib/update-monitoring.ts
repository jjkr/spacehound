import { Duration, Size } from "aws-cdk-lib";
import type * as acm from "aws-cdk-lib/aws-certificatemanager";
import type * as cloudfront from "aws-cdk-lib/aws-cloudfront";
import * as cloudwatch from "aws-cdk-lib/aws-cloudwatch";
import type * as s3 from "aws-cdk-lib/aws-s3";
import * as synthetics from "aws-cdk-lib/aws-synthetics";
import path from "node:path";
import { Construct } from "constructs";

export interface UpdateMonitoringProps {
  readonly artifactBucket: s3.IBucket;
  readonly bandwidthAlarmGibPerHour: number;
  readonly certificate: acm.ICertificate;
  readonly distribution: cloudfront.Distribution;
  readonly domainName: string;
  readonly environmentName: "beta" | "production";
}

export class UpdateMonitoring extends Construct {
  public readonly canary: synthetics.Canary;
  public readonly dashboard: cloudwatch.Dashboard;

  public constructor(scope: Construct, id: string, props: UpdateMonitoringProps) {
    super(scope, id);

    const fiveMinutes = Duration.minutes(5);
    this.canary = new synthetics.Canary(this, "EndpointCanary", {
      canaryName: `sh-${props.environmentName}-updates`,
      environmentVariables: {
        BASE_URL: `https://${props.domainName}`,
      },
      failureRetentionPeriod: Duration.days(31),
      maxRetries: 0,
      runtime: synthetics.Runtime.SYNTHETICS_NODEJS_PUPPETEER_12_0,
      schedule: synthetics.Schedule.rate(fiveMinutes),
      successRetentionPeriod: Duration.days(14),
      test: synthetics.Test.custom({
        code: synthetics.Code.fromAsset(
          path.join(__dirname, "../canary"),
        ),
        handler: "update-endpoint.handler",
      }),
    });

    const success = this.canary.metricSuccessPercent({
      label: "Endpoint success",
      period: fiveMinutes,
      statistic: "Average",
    });
    const duration = this.canary.metricDuration({
      label: "Endpoint check duration",
      period: fiveMinutes,
      statistic: "Average",
    });
    const requests = props.distribution.metricRequests({
      label: "Requests",
      period: fiveMinutes,
      statistic: "Sum",
    });
    const error4xx = props.distribution.metric4xxErrorRate({
      label: "4xx error rate",
      period: fiveMinutes,
      statistic: "Average",
    });
    const error5xx = props.distribution.metric5xxErrorRate({
      label: "5xx error rate",
      period: fiveMinutes,
      statistic: "Average",
    });
    const gated4xx = new cloudwatch.MathExpression({
      expression: "IF(requests >= 20, errors, 0)",
      label: "4xx rate (20+ requests)",
      period: fiveMinutes,
      usingMetrics: { errors: error4xx, requests },
    });
    const gated5xx = new cloudwatch.MathExpression({
      expression: "IF(requests >= 20, errors, 0)",
      label: "5xx rate (20+ requests)",
      period: fiveMinutes,
      usingMetrics: { errors: error5xx, requests },
    });
    const hourlyBytes = props.distribution.metricBytesDownloaded({
      label: "Bytes downloaded",
      period: Duration.hours(1),
      statistic: "Sum",
    });
    const certificateDays = props.certificate.metricDaysToExpiry({
      label: "Certificate days to expiry",
      period: Duration.days(1),
      statistic: "Minimum",
    });

    const alarms = [
      new cloudwatch.Alarm(this, "EndpointAlarm", {
        alarmDescription: `The ${props.environmentName} update feed or current downloads failed validation.`,
        alarmName: `SpaceHound-${props.environmentName}-UpdateEndpoint`,
        comparisonOperator: cloudwatch.ComparisonOperator.LESS_THAN_THRESHOLD,
        datapointsToAlarm: 2,
        evaluationPeriods: 3,
        metric: success,
        threshold: 100,
        treatMissingData: cloudwatch.TreatMissingData.BREACHING,
      }),
      new cloudwatch.Alarm(this, "CloudFront4xxAlarm", {
        alarmDescription: `The ${props.environmentName} distribution exceeded a 10% 4xx rate with at least 20 requests.`,
        alarmName: `SpaceHound-${props.environmentName}-CloudFront4xx`,
        comparisonOperator: cloudwatch.ComparisonOperator.GREATER_THAN_THRESHOLD,
        datapointsToAlarm: 2,
        evaluationPeriods: 3,
        metric: gated4xx,
        threshold: 10,
        treatMissingData: cloudwatch.TreatMissingData.NOT_BREACHING,
      }),
      new cloudwatch.Alarm(this, "CloudFront5xxAlarm", {
        alarmDescription: `The ${props.environmentName} distribution exceeded a 5% 5xx rate with at least 20 requests.`,
        alarmName: `SpaceHound-${props.environmentName}-CloudFront5xx`,
        comparisonOperator: cloudwatch.ComparisonOperator.GREATER_THAN_THRESHOLD,
        datapointsToAlarm: 2,
        evaluationPeriods: 3,
        metric: gated5xx,
        threshold: 5,
        treatMissingData: cloudwatch.TreatMissingData.NOT_BREACHING,
      }),
      new cloudwatch.Alarm(this, "BandwidthAlarm", {
        alarmDescription: `The ${props.environmentName} distribution downloaded more than ${props.bandwidthAlarmGibPerHour} GiB in one hour.`,
        alarmName: `SpaceHound-${props.environmentName}-HourlyBandwidth`,
        comparisonOperator: cloudwatch.ComparisonOperator.GREATER_THAN_THRESHOLD,
        evaluationPeriods: 1,
        metric: hourlyBytes,
        threshold: Size.gibibytes(props.bandwidthAlarmGibPerHour).toBytes(),
        treatMissingData: cloudwatch.TreatMissingData.NOT_BREACHING,
      }),
      new cloudwatch.Alarm(this, "CertificateExpiryAlarm", {
        alarmDescription: `The ${props.environmentName} update certificate expires in fewer than 30 days.`,
        alarmName: `SpaceHound-${props.environmentName}-CertificateExpiry`,
        comparisonOperator: cloudwatch.ComparisonOperator.LESS_THAN_THRESHOLD,
        evaluationPeriods: 1,
        metric: certificateDays,
        threshold: 30,
        treatMissingData: cloudwatch.TreatMissingData.NOT_BREACHING,
      }),
    ];

    const cacheHitRate = props.distribution.metricCacheHitRate({
      label: "Cache hit rate",
      period: fiveMinutes,
      statistic: "Average",
    });
    const originLatency = props.distribution.metricOriginLatency({
      label: "Origin latency",
      period: fiveMinutes,
      statistic: "Average",
    });
    const bucketSize = new cloudwatch.Metric({
      dimensionsMap: {
        BucketName: props.artifactBucket.bucketName,
        StorageType: "StandardStorage",
      },
      label: "Stored bytes",
      metricName: "BucketSizeBytes",
      namespace: "AWS/S3",
      period: Duration.days(1),
      statistic: "Average",
      unit: cloudwatch.Unit.BYTES,
    });
    const objectCount = new cloudwatch.Metric({
      dimensionsMap: {
        BucketName: props.artifactBucket.bucketName,
        StorageType: "AllStorageTypes",
      },
      label: "Objects",
      metricName: "NumberOfObjects",
      namespace: "AWS/S3",
      period: Duration.days(1),
      statistic: "Average",
      unit: cloudwatch.Unit.COUNT,
    });

    this.dashboard = new cloudwatch.Dashboard(this, "Dashboard", {
      dashboardName: `SpaceHound-${props.environmentName}-UpdateDelivery`,
      defaultInterval: Duration.days(1),
      periodOverride: cloudwatch.PeriodOverride.INHERIT,
    });
    this.dashboard.addWidgets(new cloudwatch.AlarmStatusWidget({
      alarms,
      height: 4,
      sortBy: cloudwatch.AlarmStatusWidgetSortBy.STATE_UPDATED_TIMESTAMP,
      title: `${props.environmentName} alarm status`,
      width: 24,
    }));
    this.dashboard.addWidgets(
      new cloudwatch.GraphWidget({
        left: [success],
        leftYAxis: { max: 100, min: 0 },
        title: "Endpoint success",
        width: 12,
      }),
      new cloudwatch.GraphWidget({
        left: [duration],
        title: "Endpoint check duration",
        width: 12,
      }),
    );
    this.dashboard.addWidgets(
      new cloudwatch.GraphWidget({
        left: [requests],
        title: "CloudFront requests",
        width: 12,
      }),
      new cloudwatch.GraphWidget({
        left: [hourlyBytes],
        title: "CloudFront bytes downloaded",
        width: 12,
      }),
    );
    this.dashboard.addWidgets(
      new cloudwatch.GraphWidget({
        left: [error4xx, error5xx],
        leftYAxis: { min: 0 },
        title: "CloudFront error rates",
        width: 12,
      }),
      new cloudwatch.GraphWidget({
        left: [cacheHitRate],
        leftYAxis: { max: 100, min: 0 },
        right: [originLatency],
        title: "Cache and origin performance",
        width: 12,
      }),
    );
    this.dashboard.addWidgets(
      new cloudwatch.SingleValueWidget({
        metrics: [certificateDays],
        title: "Certificate lifetime",
        width: 8,
      }),
      new cloudwatch.SingleValueWidget({
        metrics: [bucketSize],
        title: "Artifact storage",
        width: 8,
      }),
      new cloudwatch.SingleValueWidget({
        metrics: [objectCount],
        title: "Artifact objects",
        width: 8,
      }),
    );
  }
}
