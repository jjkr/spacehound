#import "SRPermissions.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>

@implementation SRPermissions

+ (BOOL)hasAccessibilityAccess {
  return AXIsProcessTrusted() != NO;
}

+ (void)requestAccessibilityAccess {
  NSDictionary *options = @{(__bridge NSString *)kAXTrustedCheckOptionPrompt : @YES};
  (void)AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}

+ (void)openAccessibilitySettings {
  NSURL *url = [NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?"
                                    @"Privacy_Accessibility"];
  [[NSWorkspace sharedWorkspace] openURL:url];
}

@end
