#import <Cocoa/Cocoa.h>

#import "AppDelegate.h"
#import "SRSentryMonitoring.h"

int main(int argc, const char *argv[]) {
  @autoreleasepool {
    SRStartSentryMonitoring();
    NSApplication *application = [NSApplication sharedApplication];
    AppDelegate *delegate = [[AppDelegate alloc] init];
    application.delegate = delegate;
    [application run];
  }

  return EXIT_SUCCESS;
}
