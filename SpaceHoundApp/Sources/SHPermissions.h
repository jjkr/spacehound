#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// Thin wrapper around the macOS Accessibility (AX) trust APIs and the System
// Settings deep link used to walk the user through granting access.
@interface SHPermissions : NSObject

// Whether this process is currently trusted for Accessibility access. Calling
// this also registers the app in the Accessibility list (without any prompt).
+ (BOOL)hasAccessibilityAccess;

// Opens System Settings directly to Privacy & Security > Accessibility.
+ (void)openAccessibilitySettings;

@end

NS_ASSUME_NONNULL_END
