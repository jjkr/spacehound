#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface SRRuntimeHost : NSObject

@property(nonatomic, copy, readonly) NSString *statusText;
@property(nonatomic, copy, nullable) void (^statusChangeHandler)(NSString *statusText);

- (void)start;
- (void)stop;

@end

NS_ASSUME_NONNULL_END
