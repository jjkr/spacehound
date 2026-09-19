#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface SHRuntimeHost : NSObject

@property(nonatomic, copy, readonly) NSString *statusText;
@property(nonatomic, copy, nullable) void (^stateChangeHandler)(NSString *statusText);

- (BOOL)applySettings:(NSError *_Nullable *_Nullable)error;
- (void)start;
- (void)stop;

// Suspends or resumes global hotkey/gesture interception so a shortcut editor
// can capture a key combination without the runtime acting on it.
- (void)setInputSuspended:(BOOL)suspended;

@end

NS_ASSUME_NONNULL_END
