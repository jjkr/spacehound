#import "SRRuntimeHost.h"
#import "SRSettingsStore.h"

#include <spacerabbit/daemon.hpp>

namespace {

auto settings_file_path(NSURL *url) -> std::filesystem::path {
  return std::filesystem::path([url fileSystemRepresentation]);
}

}  // namespace

@interface SRRuntimeHost ()

@property(nonatomic, copy) NSString *statusText;
@property(nonatomic, copy) NSString *menuBarTitle;

- (void)handleWorkspaceStateChangeWithCurrentSpace:(NSUInteger)currentSpace
                                         numSpaces:(NSUInteger)numSpaces;
- (NSError *)runtimeNSErrorForError:(const spacerabbit::daemon::error &)error;
- (BOOL)startRuntime:(NSError *_Nullable *_Nullable)error;
- (void)refreshRuntimeState;
- (NSString *)statusTextForError:(const spacerabbit::daemon::error &)error;
- (void)updateMenuBarTitle:(NSString *)menuBarTitle statusText:(NSString *)statusText;

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

  NSError *runtimeError = nil;
  if (![self startRuntime:&runtimeError]) {
    NSString *message = runtimeError.localizedDescription ?: @"Failed to start";
    [self updateMenuBarTitle:@"" statusText:message];
    return;
  }

  [self refreshRuntimeState];
}

- (void)stop {
  if (_runtime.running()) {
    _runtime.stop();
  }

  [self updateMenuBarTitle:@"" statusText:@"Stopped"];
}

- (BOOL)applySettings:(NSError *_Nullable *_Nullable)error {
  if (_runtime.running()) {
    const auto reloaded = _runtime.reload_settings();
    if (!reloaded.has_value()) {
      if (error != NULL) {
        *error = [self runtimeNSErrorForError:reloaded.error()];
      }
      return NO;
    }

    [self refreshRuntimeState];
    return YES;
  }

  if (![self startRuntime:error]) {
    return NO;
  }

  [self refreshRuntimeState];
  return YES;
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

- (NSError *)runtimeNSErrorForError:(const spacerabbit::daemon::error &)error {
  return [NSError errorWithDomain:@"com.animaslabs.SpaceRabbit.Runtime"
                             code:static_cast<NSInteger>(error.code)
                         userInfo:@{NSLocalizedDescriptionKey : [self statusTextForError:error]}];
}

- (BOOL)startRuntime:(NSError *_Nullable *_Nullable)error {
  NSURL *settingsURL = [SRSettingsStore settingsFileURL:error];
  if (settingsURL == nil) {
    return NO;
  }

  spacerabbit::daemon::options options;
  options.settings_path_override = settings_file_path(settingsURL);
  options.observer.active_space_changed = SRRuntimeHostActiveSpaceChanged;
  options.observer.context = (__bridge void *)self;

  const auto started = _runtime.start(options);
  if (!started.has_value()) {
    if (error != NULL) {
      *error = [self runtimeNSErrorForError:started.error()];
    }
    return NO;
  }

  return YES;
}

- (void)refreshRuntimeState {
  const auto workspaceState = _runtime.current_workspace_state();
  if (workspaceState.has_value()) {
    [self handleWorkspaceStateChangeWithCurrentSpace:static_cast<NSUInteger>(workspaceState->current_space)
                                          numSpaces:static_cast<NSUInteger>(workspaceState->num_spaces)];
    return;
  }

  [self updateMenuBarTitle:@"" statusText:@"Running"];
}

- (void)updateMenuBarTitle:(NSString *)menuBarTitle statusText:(NSString *)statusText {
  _menuBarTitle = [menuBarTitle copy];
  _statusText = [statusText copy];

  if (self.stateChangeHandler != nil) {
    self.stateChangeHandler(_menuBarTitle, _statusText);
  }
}

@end
