// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#import <Cocoa/Cocoa.h>

#import "AppDelegate.h"
#import "SHLogging.h"
#import "SHSentryMonitoring.h"

int main(int argc, const char *argv[]) {
  @autoreleasepool {
    os_log_info(SHLogLifecycle(), "Process starting");
    SHStartSentryMonitoring();
    NSApplication *application = [NSApplication sharedApplication];
    AppDelegate *delegate = [[AppDelegate alloc] init];
    application.delegate = delegate;
    [application run];
    os_log_info(SHLogLifecycle(), "Application run loop exited");
  }

  return EXIT_SUCCESS;
}
