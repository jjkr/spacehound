#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@interface SRHotkeyItem : NSObject

@property(nonatomic, copy) NSString *actionID;
@property(nonatomic, copy) NSString *sectionTitle;
@property(nonatomic, copy) NSString *displayName;
@property(nonatomic, copy) NSString *key;
@property(nonatomic, copy) NSString *modifiersText;
@property(nonatomic, assign) BOOL enabled;

@end

@interface SRSettingsDocument : NSObject

@property(nonatomic, copy) NSString *version;
@property(nonatomic, assign) BOOL workspaceWrap;
@property(nonatomic, assign) BOOL displayWrap;
@property(nonatomic, assign) BOOL trayScroll;
@property(nonatomic, assign) BOOL trayScrollInverted;
@property(nonatomic, assign) BOOL fastSwipe;
@property(nonatomic, copy) NSArray<SRHotkeyItem *> *hotkeys;

@end

@interface SRSettingsStore : NSObject

+ (nullable NSURL *)settingsFileURL:(NSError **)error;
+ (nullable SRSettingsDocument *)loadDocument:(NSError **)error;
+ (BOOL)saveDocument:(SRSettingsDocument *)document error:(NSError **)error;

@end

NS_ASSUME_NONNULL_END
