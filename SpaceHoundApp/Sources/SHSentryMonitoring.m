#import "SHSentryMonitoring.h"

#import "SHLogging.h"

#import <Sentry/Sentry.h>

static NSString *_Nullable SHNonEmptyString(id _Nullable value) {
  if (![value isKindOfClass:[NSString class]]) {
    return nil;
  }

  NSString *trimmed =
      [(NSString *)value stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
  return trimmed.length > 0 ? trimmed : nil;
}

static NSString *_Nullable SHSentryDSN(void) {
  NSString *bundledDSN =
      SHNonEmptyString([[NSBundle mainBundle] objectForInfoDictionaryKey:@"SentryDSN"]);
  if (bundledDSN != nil) {
    return bundledDSN;
  }

  return SHNonEmptyString([NSProcessInfo processInfo].environment[@"SENTRY_DSN"]);
}

static NSString *SHSentryReleaseName(NSBundle *bundle) {
  NSString *bundleIdentifier =
      SHNonEmptyString(bundle.bundleIdentifier) ?: @"com.jjkr.spacehound";
  NSString *marketingVersion =
      SHNonEmptyString([bundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"]) ?: @"0.0.0";
  NSString *buildVersion =
      SHNonEmptyString([bundle objectForInfoDictionaryKey:@"CFBundleVersion"]) ?: @"0";
  return [NSString stringWithFormat:@"%@@%@+%@", bundleIdentifier, marketingVersion, buildVersion];
}

void SHStartSentryMonitoring(void) {
  NSString *dsn = SHSentryDSN();
  if (dsn == nil) {
    os_log_info(SHLogLifecycle(), "Crash monitoring disabled because no DSN is configured");
    return;
  }

  NSBundle *bundle = [NSBundle mainBundle];
  NSString *buildVersion =
      SHNonEmptyString([bundle objectForInfoDictionaryKey:@"CFBundleVersion"]) ?: @"0";

  [SentrySDK startWithConfigureOptions:^(SentryOptions *options) {
    options.dsn = dsn;
    options.releaseName = SHSentryReleaseName(bundle);
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

  os_log_info(SHLogLifecycle(), "Crash monitoring started");
}
