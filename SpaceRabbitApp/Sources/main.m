#import <Cocoa/Cocoa.h>

#import "AppDelegate.h"
#import "SRLogging.h"
#import "SRSentryMonitoring.h"

int main(int argc, const char *argv[]) {
  @autoreleasepool {
    os_log_info(SRLogLifecycle(), "Process starting");
    SRStartSentryMonitoring();
    NSApplication *application = [NSApplication sharedApplication];
    AppDelegate *delegate = [[AppDelegate alloc] init];
    application.delegate = delegate;
    [application run];
    os_log_info(SRLogLifecycle(), "Application run loop exited");
  }

  return EXIT_SUCCESS;
}
