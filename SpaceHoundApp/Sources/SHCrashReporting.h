#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// The user's crash-reporting opt-in. Stored in NSUserDefaults rather than
// settings.json because it belongs to the app shell, not the runtime, and it
// must be readable in main() before settings.json is touched.
@interface SHCrashReporting : NSObject

// NO until the user has answered the first-launch prompt or saved Settings.
+ (BOOL)hasRecordedChoice;

// Whether crash reports may be sent. NO while no choice has been recorded.
+ (BOOL)isEnabled;
+ (void)setEnabled:(BOOL)enabled;

@end

NS_ASSUME_NONNULL_END
