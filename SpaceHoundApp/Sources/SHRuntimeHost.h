// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface SHRuntimeHost : NSObject

// The current Space number while the runtime is running, otherwise empty.
@property(nonatomic, copy, readonly) NSString *menuBarTitle;
// Why the last start attempt failed; nil while the runtime is stopped or running.
@property(nonatomic, copy, readonly, nullable) NSString *startError;
@property(nonatomic, copy, nullable) void (^stateChangeHandler)(NSString *menuBarTitle,
                                                                NSString *_Nullable startError);

- (BOOL)applySettings:(NSError *_Nullable *_Nullable)error;
- (void)start;
- (void)stop;

// Suspends or resumes global hotkey/gesture interception so a shortcut editor
// can capture a key combination without the runtime acting on it.
- (void)setInputSuspended:(BOOL)suspended;

@end

NS_ASSUME_NONNULL_END
