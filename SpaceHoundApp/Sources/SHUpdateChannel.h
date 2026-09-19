// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// The user's Sparkle update-channel opt-in. Stored in NSUserDefaults rather
// than settings.json because it belongs to the app shell, not the runtime.
@interface SHUpdateChannel : NSObject

// Whether beta releases are offered alongside production releases.
+ (BOOL)receivesBetaUpdates;
+ (void)setReceivesBetaUpdates:(BOOL)enabled;

@end

NS_ASSUME_NONNULL_END
