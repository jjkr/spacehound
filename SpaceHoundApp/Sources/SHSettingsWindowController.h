// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

typedef BOOL (^SHSettingsApplyHandler)(NSError **error);

@interface SHSettingsWindowController : NSWindowController <NSWindowDelegate>

@property(nonatomic, copy, nullable) SHSettingsApplyHandler applyHandler;

// Invoked while a shortcut is being recorded so the caller can suspend and
// resume global hotkey handling.
@property(nonatomic, copy, nullable) void (^inputSuspensionHandler)(BOOL suspended);

// YES when the Sparkle updater is configured. The beta-updates toggle is
// disabled otherwise.
@property(nonatomic, assign) BOOL updateChannelSelectable;

// Invoked after a save when the beta-updates opt-in actually changed.
@property(nonatomic, copy, nullable) void (^updateChannelChangedHandler)(void);

// YES when a Sentry DSN is configured. The crash-reporting toggle is disabled
// otherwise.
@property(nonatomic, assign) BOOL crashReportingAvailable;

// Invoked after a save when the crash-reporting opt-in actually changed.
@property(nonatomic, copy, nullable) void (^crashReportingChangedHandler)(void);

// YES while the window is open and its controls differ from what was last
// loaded from or written to disk.
@property(nonatomic, readonly) BOOL hasUnsavedChanges;

- (void)showWindowAndActivate;

// Presents the Save / Cancel / Don't Save sheet. The completion receives YES
// when the caller may proceed (the user chose Don't Save, or chose Save and
// the save succeeded) and NO when the user cancelled or the save failed.
- (void)confirmDiscardingUnsavedChanges:(void (^)(BOOL proceed))completion;

@end

NS_ASSUME_NONNULL_END
