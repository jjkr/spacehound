#import "SRRuntimeHost.h"

#include <spacerabbit/daemon.hpp>

@interface SRRuntimeHost ()

@property(nonatomic, copy) NSString *statusText;
@property(nonatomic, copy) NSString *menuBarTitle;

- (void)handleWorkspaceStateChangeWithCurrentSpace:(NSUInteger)currentSpace
                                         numSpaces:(NSUInteger)numSpaces;
- (NSString *)statusTextForError:(const spacerabbit::daemon::error &)error;
- (void)updateMenuBarTitle:(NSString *)menuBarTitle statusText:(NSString *)statusText;
- (BOOL)ensureDefaultSettingsFileExists:(NSURL *_Nullable *_Nullable)settingsURL
                                  error:(NSError *_Nullable *_Nullable)error;

@end

@implementation SRRuntimeHost {
  spacerabbit::daemon::runtime _runtime;
}

static void SRRuntimeHostActiveSpaceChanged(
    const spacerabbit::daemon::workspace_state &state,
    void *context) {
  SRRuntimeHost *host = (__bridge SRRuntimeHost *)context;
  __weak SRRuntimeHost *weakHost = host;
  const NSUInteger currentSpace = static_cast<NSUInteger>(state.current_space);
  const NSUInteger numSpaces = static_cast<NSUInteger>(state.num_spaces);

  dispatch_async(dispatch_get_main_queue(), ^{
    [weakHost handleWorkspaceStateChangeWithCurrentSpace:currentSpace numSpaces:numSpaces];
  });
}

- (instancetype)init {
  self = [super init];
  if (self == nil) {
    return nil;
  }

  _statusText = @"Stopped";
  _menuBarTitle = @"";
  return self;
}

- (void)dealloc {
  [self stop];
}

- (void)start {
  if (_runtime.running()) {
    return;
  }

  [self updateMenuBarTitle:@"" statusText:@"Starting..."];

  NSURL *settingsURL = nil;
  NSError *settingsError = nil;
  if (![self ensureDefaultSettingsFileExists:&settingsURL error:&settingsError]) {
    NSString *message = settingsError.localizedDescription ?: @"Failed to prepare settings";
    [self updateMenuBarTitle:@"" statusText:message];
    return;
  }

  spacerabbit::daemon::options options;
  options.settings_path_override = std::filesystem::path(settingsURL.path.UTF8String);
  options.observer.active_space_changed = SRRuntimeHostActiveSpaceChanged;
  options.observer.context = (__bridge void *)self;

  const auto started = _runtime.start(options);
  if (!started.has_value()) {
    NSString *message = [self statusTextForError:started.error()];
    NSLog(@"SpaceRabbit runtime start failed: %@", message);
    [self updateMenuBarTitle:@"" statusText:message];
    return;
  }

  const auto workspaceState = _runtime.current_workspace_state();
  if (workspaceState.has_value()) {
    [self handleWorkspaceStateChangeWithCurrentSpace:static_cast<NSUInteger>(workspaceState->current_space)
                                          numSpaces:static_cast<NSUInteger>(workspaceState->num_spaces)];
    return;
  }

  [self updateMenuBarTitle:@"" statusText:@"Running"];
}

- (void)stop {
  if (_runtime.running()) {
    _runtime.stop();
  }

  [self updateMenuBarTitle:@"" statusText:@"Stopped"];
}

- (void)handleWorkspaceStateChangeWithCurrentSpace:(NSUInteger)currentSpace
                                         numSpaces:(NSUInteger)numSpaces {
  if (currentSpace == 0 || numSpaces == 0) {
    [self updateMenuBarTitle:@"" statusText:@"Running"];
    return;
  }

  [self updateMenuBarTitle:[NSString stringWithFormat:@"%lu", (unsigned long)currentSpace]
                statusText:[NSString stringWithFormat:@"Space %lu of %lu",
                                                      (unsigned long)currentSpace,
                                                      (unsigned long)numSpaces]];
}

- (NSString *)statusTextForError:(const spacerabbit::daemon::error &)error {
  if (!error.message.empty()) {
    return [NSString stringWithUTF8String:error.message.c_str()];
  }

  switch (error.code) {
    case spacerabbit::daemon::error_code::settings_error:
      return @"Settings error";
    case spacerabbit::daemon::error_code::permission_denied:
      return @"Permission required";
    case spacerabbit::daemon::error_code::state_unavailable:
      return @"State unavailable";
    case spacerabbit::daemon::error_code::already_running:
      return @"Already running";
    case spacerabbit::daemon::error_code::not_running:
      return @"Not running";
    case spacerabbit::daemon::error_code::runtime_error:
      return @"Runtime error";
  }

  return @"Runtime error";
}

- (void)updateMenuBarTitle:(NSString *)menuBarTitle statusText:(NSString *)statusText {
  _menuBarTitle = [menuBarTitle copy];
  _statusText = [statusText copy];

  if (self.stateChangeHandler != nil) {
    self.stateChangeHandler(_menuBarTitle, _statusText);
  }
}

- (BOOL)ensureDefaultSettingsFileExists:(NSURL *_Nullable *_Nullable)settingsURL
                                  error:(NSError *_Nullable *_Nullable)error {
  NSFileManager *fileManager = [NSFileManager defaultManager];
  NSURL *applicationSupportDirectory =
      [[fileManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask] firstObject];
  if (applicationSupportDirectory == nil) {
    if (error != NULL) {
      *error = [NSError errorWithDomain:NSCocoaErrorDomain
                                   code:NSFileNoSuchFileError
                               userInfo:@{NSLocalizedDescriptionKey : @"Application Support directory not found"}];
    }
    return NO;
  }

  NSURL *spaceRabbitDirectory = [applicationSupportDirectory URLByAppendingPathComponent:@"SpaceRabbit"
                                                                              isDirectory:YES];
  if (![fileManager createDirectoryAtURL:spaceRabbitDirectory
             withIntermediateDirectories:YES
                              attributes:nil
                                   error:error]) {
    return NO;
  }

  NSURL *resolvedSettingsURL = [spaceRabbitDirectory URLByAppendingPathComponent:@"settings.json"];
  if (![fileManager fileExistsAtPath:resolvedSettingsURL.path]) {
    NSDictionary *defaultSettings = @{
      @"version" : @"1.0",
      @"workspaceWrap" : @NO,
      @"displayWrap" : @NO,
      @"trayScroll" : @YES,
      @"trayScrollInverted" : @NO,
      @"hotkeys" : @{},
      @"fastSwipe" : @YES,
      @"telemetryEnabled" : @YES,
    };
    NSData *settingsData = [NSJSONSerialization dataWithJSONObject:defaultSettings
                                                           options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys
                                                             error:error];
    if (settingsData == nil) {
      return NO;
    }

    if (![settingsData writeToURL:resolvedSettingsURL options:NSDataWritingAtomic error:error]) {
      return NO;
    }
  }

  if (settingsURL != NULL) {
    *settingsURL = resolvedSettingsURL;
  }

  return YES;
}

@end
