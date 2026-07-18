#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, SRLoginItemStatus) {
  SRLoginItemStatusNotRegistered,
  SRLoginItemStatusEnabled,
  SRLoginItemStatusRequiresApproval,
  SRLoginItemStatusNotFound,
};

@interface SRLoginItemManager : NSObject

+ (SRLoginItemStatus)status;
+ (BOOL)setEnabled:(BOOL)enabled error:(NSError **)error;
+ (void)openSystemSettings;

@end

NS_ASSUME_NONNULL_END
