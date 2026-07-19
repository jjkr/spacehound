#import "SRSentryMonitoring.h"

#import <Sentry/Sentry.h>

static NSString *_Nullable SRNonEmptyString(id _Nullable value) {
  if (![value isKindOfClass:[NSString class]]) {
    return nil;
  }

  NSString *trimmed =
      [(NSString *)value stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
  return trimmed.length > 0 ? trimmed : nil;
}

static NSString *_Nullable SRSentryDSN(void) {
  NSString *bundledDSN =
      SRNonEmptyString([[NSBundle mainBundle] objectForInfoDictionaryKey:@"SentryDSN"]);
  if (bundledDSN != nil) {
    return bundledDSN;
  }

  return SRNonEmptyString([NSProcessInfo processInfo].environment[@"SENTRY_DSN"]);
}

static NSString *SRSentryReleaseName(NSBundle *bundle) {
  NSString *bundleIdentifier =
      SRNonEmptyString(bundle.bundleIdentifier) ?: @"com.animaslabs.SpaceRabbit";
  NSString *marketingVersion =
      SRNonEmptyString([bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"]) ?: @"0.0.0";
  NSString *buildVersion =
      SRNonEmptyString([bundle objectForInfoDictionaryKey:@"CFBundleVersion"]) ?: @"0";
  return [NSString stringWithFormat:@"%@@%@+%@", bundleIdentifier, marketingVersion, buildVersion];
}

void SRStartSentryMonitoring(void) {
  NSString *dsn = SRSentryDSN();
  if (dsn == nil) {
    return;
  }

  NSBundle *bundle = [NSBundle mainBundle];
  NSString *buildVersion =
      SRNonEmptyString([bundle objectForInfoDictionaryKey:@"CFBundleVersion"]) ?: @"0";

  [SentrySDK startWithConfigureOptions:^(SentryOptions *options) {
    options.dsn = dsn;
    options.releaseName = SRSentryReleaseName(bundle);
    options.dist = buildVersion;
    options.environment = @"production";
    options.sampleRate = @1.0;

    options.enableCrashHandler = YES;
    options.enableUncaughtNSExceptionReporting = YES;
    options.enableSwizzling = YES;

    options.sendDefaultPii = NO;
    options.enableAutoSessionTracking = NO;
    options.sendClientReports = NO;
    options.enableLogs = NO;
    options.maxBreadcrumbs = 0;
    options.enableAutoBreadcrumbTracking = NO;
    options.enableNetworkBreadcrumbs = NO;
    options.enableCaptureFailedRequests = NO;
    options.enableSigtermReporting = NO;
    options.enableWatchdogTerminationTracking = NO;
    options.enableAppHangTracking = NO;
    options.enableMetricKit = NO;
    options.enableMetricKitRawPayload = NO;

    options.tracesSampleRate = @0.0;
    options.enableAutoPerformanceTracing = NO;
    options.enablePersistingTracesWhenCrashing = NO;
    options.enableNetworkTracking = NO;
    options.enableFileIOTracing = NO;
    options.enableDataSwizzling = NO;
    options.enableFileManagerSwizzling = NO;
    options.enableCoreDataTracing = NO;
    options.attachStacktrace = NO;
    options.attachAllThreads = NO;
  }];
}
