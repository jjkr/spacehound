#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

typedef BOOL (^SHSettingsApplyHandler)(NSError **error);

@interface SHSettingsWindowController : NSWindowController <NSWindowDelegate>

@property(nonatomic, copy, nullable) SHSettingsApplyHandler applyHandler;

// Invoked while a shortcut is being recorded so the caller can suspend and
// resume global hotkey handling.
@property(nonatomic, copy, nullable) void (^inputSuspensionHandler)(BOOL suspended);

- (void)showWindowAndActivate;

@end

NS_ASSUME_NONNULL_END
