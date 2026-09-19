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

// Invoked after Save when the beta-updates opt-in actually changed.
@property(nonatomic, copy, nullable) void (^updateChannelChangedHandler)(void);

- (void)showWindowAndActivate;

@end

NS_ASSUME_NONNULL_END
