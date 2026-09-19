// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, SHLoginItemStatus) {
  SHLoginItemStatusNotRegistered,
  SHLoginItemStatusEnabled,
  SHLoginItemStatusRequiresApproval,
  SHLoginItemStatusNotFound,
};

@interface SHLoginItemManager : NSObject

+ (SHLoginItemStatus)status;
+ (BOOL)setEnabled:(BOOL)enabled error:(NSError **)error;
+ (void)openSystemSettings;

@end

NS_ASSUME_NONNULL_END
