#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// Thin wrapper around the macOS Accessibility (AX) trust APIs and the System
// Settings deep link used to walk the user through granting access.
@interface SRPermissions : NSObject

// Whether this process is currently trusted for Accessibility access.
+ (BOOL)hasAccessibilityAccess;

// Registers this app in the Accessibility list and shows the system's native
// "grant access" prompt. Safe to call repeatedly.
+ (void)requestAccessibilityAccess;

// Opens System Settings directly to Privacy & Security > Accessibility.
+ (void)openAccessibilitySettings;

@end

NS_ASSUME_NONNULL_END
