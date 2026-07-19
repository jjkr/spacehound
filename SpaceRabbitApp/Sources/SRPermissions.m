#import "SRPermissions.h"

#import "SRLogging.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

@implementation SRPermissions

+ (BOOL)hasAccessibilityAccess {
  return AXIsProcessTrusted() != NO;
}

+ (void)openAccessibilitySettings {
  NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?"
                                    @"Privacy_Accessibility"];
  if ([[NSWorkspace sharedWorkspace] openURL:url]) {
    os_log_info(SRLogPermissions(), "Opened Accessibility settings");
  } else {
    os_log_error(SRLogPermissions(), "Failed to open Accessibility settings");
  }
}

@end
