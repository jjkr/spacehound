#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface SRRuntimeHost : NSObject

@property(nonatomic, copy, readonly) NSString *statusText;
@property(nonatomic, copy, readonly) NSString *menuBarTitle;
@property(nonatomic, copy, nullable) void (^stateChangeHandler)(NSString *menuBarTitle, NSString *statusText);

- (BOOL)applySettings:(NSError *_Nullable *_Nullable)error;
- (void)start;
- (void)stop;

@end

NS_ASSUME_NONNULL_END
