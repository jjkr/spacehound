// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// Thin wrapper around the macOS Accessibility (AX) trust APIs and the System
// Settings deep link used to walk the user through granting access.
@interface SHPermissions : NSObject

// Whether this process is currently trusted for Accessibility access. This is
// a pure query: it never prompts and never adds the app to the Accessibility
// list.
+ (BOOL)hasAccessibilityAccess;

// Asks macOS to register this app in the Accessibility list and, if it is not
// already listed, shows the system "would like to control this computer"
// dialog. This is the only way to (re)create the app's entry, so it is what
// makes the app show up in System Settings after its entry was removed or the
// binary changed (a rebuild from another worktree, for example). Returns
// whether access is currently granted.
+ (BOOL)requestAccessibilityAccess;

// Opens System Settings directly to Privacy & Security > Accessibility.
+ (void)openAccessibilitySettings;

@end

NS_ASSUME_NONNULL_END
