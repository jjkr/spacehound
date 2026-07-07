#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

typedef BOOL (^SRSettingsApplyHandler)(NSError **error);

@interface SRSettingsWindowController : NSWindowController

@property(nonatomic, copy, nullable) SRSettingsApplyHandler applyHandler;

- (void)showWindowAndActivate;

@end

NS_ASSUME_NONNULL_END
