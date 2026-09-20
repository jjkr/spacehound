#import "SHCrashReporting.h"

static NSString *const SHCrashReportingEnabledKey = @"SHCrashReportingEnabled";

@implementation SHCrashReporting

+ (BOOL)hasRecordedChoice {
  return [[NSUserDefaults standardUserDefaults] objectForKey:SHCrashReportingEnabledKey] != nil;
}

+ (BOOL)isEnabled {
  return [[NSUserDefaults standardUserDefaults] boolForKey:SHCrashReportingEnabledKey];
}

+ (void)setEnabled:(BOOL)enabled {
  [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:SHCrashReportingEnabledKey];
}

@end
