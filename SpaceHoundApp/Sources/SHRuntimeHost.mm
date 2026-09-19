#import "SHRuntimeHost.h"
#import "SHLogging.h"
#import "SHSettingsStore.h"

#include <spacehound/daemon.hpp>

namespace {

auto settings_file_path(NSURL *url) -> std::filesystem::path {
  return std::filesystem::path([url fileSystemRepresentation]);
}

}  // namespace

@interface SHRuntimeHost ()

@property(nonatomic, copy) NSString *statusText;
@property(nonatomic, copy) NSString *menuBarTitle;

- (void)handleWorkspaceStateChangeWithCurrentSpace:(NSUInteger)currentSpace
                                         numSpaces:(NSUInteger)numSpaces;
- (NSError *)runtimeNSErrorForError:(const spacehound::daemon::error &)error;
- (BOOL)startRuntime:(NSError *_Nullable *_Nullable)error;
- (void)refreshRuntimeState;
- (NSString *)statusTextForError:(const spacehound::daemon::error &)error;
- (void)updateMenuBarTitle:(NSString *)menuBarTitle statusText:(NSString *)statusText;

@end

@implementation SHRuntimeHost {
  spacehound::daemon::runtime _runtime;
}

static void SHRuntimeHostActiveSpaceChanged(
    const spacehound::daemon::workspace_state &state,
    void *context) {
  SHRuntimeHost *host = (__bridge SHRuntimeHost *)context;
  __weak SHRuntimeHost *weakHost = host;
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
    os_log_debug(SHLogLifecycle(), "Runtime start ignored because it is already running");
    return;
  }

  os_log_info(SHLogLifecycle(), "Runtime starting");
  [self updateMenuBarTitle:@"" statusText:@"Starting..."];

  NSError *runtimeError = nil;
  if (![self startRuntime:&runtimeError]) {
    os_log_error(SHLogLifecycle(),
                 "Runtime failed to start (domain=%{private}@ code=%{public}ld)",
                 runtimeError.domain,
                 (long)runtimeError.code);
    NSString *message = runtimeError.localizedDescription ?: @"Failed to start";
    [self updateMenuBarTitle:@"" statusText:message];
    return;
  }

  [self refreshRuntimeState];
  os_log_info(SHLogLifecycle(), "Runtime started");
}

- (void)stop {
  if (_runtime.running()) {
    os_log_info(SHLogLifecycle(), "Runtime stopping");
    _runtime.stop();
    os_log_info(SHLogLifecycle(), "Runtime stopped");
  }

  [self updateMenuBarTitle:@"" statusText:@"Stopped"];
}

- (BOOL)applySettings:(NSError *_Nullable *_Nullable)error {
  os_log_info(SHLogSettings(), "Applying settings to the runtime");
  if (_runtime.running()) {
    const auto reloaded = _runtime.reload_settings();
    if (!reloaded.has_value()) {
      os_log_error(SHLogSettings(),
                   "Runtime settings reload failed (code=%{public}ld)",
                   (long)static_cast<NSInteger>(reloaded.error().code));
      if (error != NULL) {
        *error = [self runtimeNSErrorForError:reloaded.error()];
      }
      return NO;
    }

    [self refreshRuntimeState];
    os_log_info(SHLogSettings(), "Runtime settings reload completed");
    return YES;
  }

  if (![self startRuntime:error]) {
    return NO;
  }

  [self refreshRuntimeState];
  os_log_info(SHLogSettings(), "Runtime started with updated settings");
  return YES;
}

- (void)setInputSuspended:(BOOL)suspended {
  _runtime.set_input_suspended(suspended ? true : false);
  if (suspended) {
    os_log_debug(SHLogNavigation(), "Global input handling suspended for shortcut recording");
  } else {
    os_log_debug(SHLogNavigation(), "Global input handling resumed after shortcut recording");
  }
}

- (void)handleWorkspaceStateChangeWithCurrentSpace:(NSUInteger)currentSpace
                                         numSpaces:(NSUInteger)numSpaces {
  if (currentSpace == 0 || numSpaces == 0) {
    os_log_debug(SHLogNavigation(), "Workspace state changed but its bounds are unavailable");
    [self updateMenuBarTitle:@"" statusText:@"Running"];
    return;
  }

  os_log_debug(SHLogNavigation(),
               "Workspace state changed (current=%{private}lu total=%{private}lu)",
               (unsigned long)currentSpace,
               (unsigned long)numSpaces);
  [self updateMenuBarTitle:[NSString stringWithFormat:@"%lu", (unsigned long)currentSpace]
                statusText:[NSString stringWithFormat:@"Space %lu of %lu",
                                                      (unsigned long)currentSpace,
                                                      (unsigned long)numSpaces]];
}

- (NSString *)statusTextForError:(const spacehound::daemon::error &)error {
  if (!error.message.empty()) {
    return [NSString stringWithUTF8String:error.message.c_str()];
  }

  switch (error.code) {
    case spacehound::daemon::error_code::settings_error:
      return @"Settings error";
    case spacehound::daemon::error_code::permission_denied:
      return @"Permission required";
    case spacehound::daemon::error_code::state_unavailable:
      return @"State unavailable";
    case spacehound::daemon::error_code::already_running:
      return @"Already running";
    case spacehound::daemon::error_code::not_running:
      return @"Not running";
    case spacehound::daemon::error_code::runtime_error:
      return @"Runtime error";
  }

  return @"Runtime error";
}

- (NSError *)runtimeNSErrorForError:(const spacehound::daemon::error &)error {
  return [NSError errorWithDomain:@"com.jjkr.spacehound.Runtime"
                             code:static_cast<NSInteger>(error.code)
                         userInfo:@{NSLocalizedDescriptionKey : [self statusTextForError:error]}];
}

- (BOOL)startRuntime:(NSError *_Nullable *_Nullable)error {
  NSURL *settingsURL = [SHSettingsStore settingsFileURL:error];
  if (settingsURL == nil) {
    NSError *settingsError = error != NULL ? *error : nil;
    os_log_error(SHLogSettings(),
                 "Runtime could not locate settings (domain=%{private}@ code=%{public}ld)",
                 settingsError.domain,
                 (long)settingsError.code);
    return NO;
  }

  spacehound::daemon::options options;
  options.settings_path_override = settings_file_path(settingsURL);
  options.observer.active_space_changed = SHRuntimeHostActiveSpaceChanged;
  options.observer.context = (__bridge void *)self;

  const auto started = _runtime.start(options);
  if (!started.has_value()) {
    os_log_error(SHLogLifecycle(),
                 "Daemon runtime start failed (code=%{public}ld)",
                 (long)static_cast<NSInteger>(started.error().code));
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

  os_log_debug(SHLogNavigation(), "Current workspace state is unavailable");
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
