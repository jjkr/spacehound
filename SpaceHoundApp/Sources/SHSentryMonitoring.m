// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#import "SHSentryMonitoring.h"

#import "SHCrashReporting.h"
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

BOOL SHCrashReportingIsAvailable(void) {
  return SHSentryDSN() != nil;
}

// Logged once per process: a restart from Settings would otherwise report the
// current run as the "previous" one.
static void SHLogLastRunStatusOnce(void) {
  static BOOL logged = NO;
  if (logged) {
    return;
  }
  logged = YES;

  switch (SentrySDK.lastRunStatus) {
    case SentryLastRunStatusDidCrash:
      os_log_error(SHLogCrashReporting(), "Previous run ended in a crash; the report is queued for upload");
      break;
    case SentryLastRunStatusDidNotCrash:
      os_log_debug(SHLogCrashReporting(), "Previous run did not crash");
      break;
    case SentryLastRunStatusUnknown:
      os_log_debug(SHLogCrashReporting(), "Previous run crash status is unknown");
      break;
  }
}

void SHStartCrashReportingIfEnabled(void) {
  NSString *dsn = SHSentryDSN();
  if (dsn == nil) {
    os_log_info(SHLogCrashReporting(), "Crash reporting disabled because no DSN is configured");
    return;
  }
  if (![SHCrashReporting hasRecordedChoice]) {
    os_log_info(SHLogCrashReporting(), "Crash reporting not started because the user has not chosen yet");
    return;
  }
  if (![SHCrashReporting isEnabled]) {
    os_log_info(SHLogCrashReporting(), "Crash reporting disabled by the user");
    return;
  }
  if (SentrySDK.isEnabled) {
    os_log_debug(SHLogCrashReporting(), "Crash reporting already running");
    return;
  }

  NSBundle *bundle = [NSBundle mainBundle];
  NSString *buildVersion =
      SHNonEmptyString([bundle objectForInfoDictionaryKey:@"CFBundleVersion"]) ?: @"0";

  [SentrySDK startWithConfigureOptions:^(SentryOptions *options) {
    options.dsn = dsn;
    options.releaseName = SHSentryReleaseName(bundle);
    options.dist = buildVersion;
    // Keeps local test crashes out of the production issue stream.
#ifdef DEBUG
    options.environment = @"development";
#else
    options.environment = @"production";
#endif
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

  os_log_info(SHLogCrashReporting(), "Crash reporting started");
  SHLogLastRunStatusOnce();
}

void SHStopCrashReporting(void) {
  if (!SentrySDK.isEnabled) {
    return;
  }
  [SentrySDK close];
  os_log_info(SHLogCrashReporting(), "Crash reporting stopped");
}

void SHCrashForTesting(void) {
  os_log_error(SHLogCrashReporting(), "Crashing deliberately to test crash reporting");
  [SentrySDK crash];
}
