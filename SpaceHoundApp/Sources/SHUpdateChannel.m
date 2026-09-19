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
