// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#import "SHPermissions.h"

#import "SHLogging.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

@implementation SHPermissions

+ (BOOL)hasAccessibilityAccess {
  return AXIsProcessTrusted() != NO;
}

+ (BOOL)requestAccessibilityAccess {
  NSDictionary *options = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt : @YES};
  const BOOL trusted = AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options) != NO;
  os_log_info(SHLogPermissions(), "Requested Accessibility access (trusted=%{public}d)", trusted);
  return trusted;
}

+ (void)openAccessibilitySettings {
  NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?"
                                    @"Privacy_Accessibility"];
  if ([[NSWorkspace sharedWorkspace] openURL:url]) {
    os_log_info(SHLogPermissions(), "Opened Accessibility settings");
  } else {
    os_log_error(SHLogPermissions(), "Failed to open Accessibility settings");
  }
}

@end
