// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#import "SHUpdateChannel.h"

static NSString *const SHReceiveBetaUpdatesKey = @"SHReceiveBetaUpdates";

@implementation SHUpdateChannel

+ (BOOL)receivesBetaUpdates {
  return [[NSUserDefaults standardUserDefaults] boolForKey:SHReceiveBetaUpdatesKey];
}

+ (void)setReceivesBetaUpdates:(BOOL)enabled {
  [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:SHReceiveBetaUpdatesKey];
}

@end
