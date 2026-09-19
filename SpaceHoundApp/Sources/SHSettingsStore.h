#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@interface SHHotkeyItem : NSObject

@property(nonatomic, copy) NSString *actionID;
@property(nonatomic, copy) NSString *sectionTitle;
@property(nonatomic, copy) NSString *displayName;
@property(nonatomic, copy) NSString *key;
@property(nonatomic, copy) NSString *modifiersText;
@property(nonatomic, assign) BOOL enabled;

@end

@interface SHSettingsDocument : NSObject

@property(nonatomic, copy) NSString *version;
@property(nonatomic, assign) BOOL workspaceWrap;
@property(nonatomic, assign) BOOL displayWrap;
@property(nonatomic, assign) BOOL trayScroll;
@property(nonatomic, assign) BOOL trayScrollInverted;
@property(nonatomic, assign) BOOL fastSwipe;
@property(nonatomic, assign) BOOL moveCursorToActiveDisplay;
@property(nonatomic, assign) BOOL moveCursorToTargetDisplay;
@property(nonatomic, copy) NSArray<SHHotkeyItem *> *hotkeys;

@end

@interface SHSettingsStore : NSObject

+ (nullable NSURL *)settingsFileURL:(NSError **)error;
+ (nullable SHSettingsDocument *)loadDocument:(NSError **)error;
+ (BOOL)saveDocument:(SHSettingsDocument *)document error:(NSError **)error;

@end

NS_ASSUME_NONNULL_END
