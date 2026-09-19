#import "SHRuntimeHost.h"
#import "SHLogging.h"
#import "SHSettingsStore.h"

#import <Cocoa/Cocoa.h>

#include <spacehound/daemon.hpp>

namespace {

auto settings_file_path(NSURL *url) -> std::filesystem::path {
  return std::filesystem::path([url fileSystemRepresentation]);
}

auto screen_for_display_uuid(NSString *uuid) -> NSScreen * {
  for (NSScreen *screen in NSScreen.screens) {
    NSNumber *screenNumber = screen.deviceDescription[@"NSScreenNumber"];
    if (screenNumber == nil) {
      continue;
    }

    CFUUIDRef displayUUID = CGDisplayCreateUUIDFromDisplayID(screenNumber.unsignedIntValue);
    if (displayUUID == NULL) {
      continue;
    }

    CFStringRef displayUUIDString = CFUUIDCreateString(kCFAllocatorDefault, displayUUID);
    CFRelease(displayUUID);
    if (displayUUIDString == NULL) {
      continue;
    }

    const BOOL matches = [(__bridge NSString *)displayUUIDString isEqualToString:uuid];
    CFRelease(displayUUIDString);
    if (matches) {
      return screen;
    }
  }

  return nil;
}

}  // namespace

// A transparent, mouse-ignoring 1x1 window that may become key. macOS makes
// the display holding the key window the active one, so parking this window on
// an otherwise empty display activates it without synthesizing a click or
// moving the cursor.
//
// This only works while the app has the Regular activation policy: accessory
// (LSUIElement) apps do not own the menu bar, so their key window never moves
// it. The host therefore switches to Regular for as long as the anchor is up
// and reverts to Accessory when it is dismissed.
@interface SHDisplayAnchorWindow : NSWindow
@end

@implementation SHDisplayAnchorWindow

- (BOOL)canBecomeKeyWindow {
  return YES;
}

- (BOOL)canBecomeMainWindow {
  return NO;
}

// While the anchor is key the menu bar reads "SpaceHound", so a reflexive
// Cmd-Q aimed at the "empty" desktop would quit the hotkey daemon. Swallow
// it; the menu item and the tray menu still quit.
- (BOOL)performKeyEquivalent:(NSEvent *)event {
  const NSEventModifierFlags modifiers =
      event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
  if (modifiers == NSEventModifierFlagCommand &&
      [event.charactersIgnoringModifiers isEqualToString:@"q"]) {
    return YES;
  }

  return [super performKeyEquivalent:event];
}

@end

@interface SHRuntimeHost ()

@property(nonatomic, copy) NSString *statusText;
@property(nonatomic, copy) NSString *menuBarTitle;
@property(nonatomic, strong, nullable) SHDisplayAnchorWindow *displayAnchorWindow;

- (BOOL)activateEmptyDisplayWithUUID:(NSString *)uuid;
- (void)dismissDisplayAnchor;

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

static bool SHRuntimeHostActivateEmptyDisplay(
    const spacehound::control::display_target &target,
    void *context) {
  SHRuntimeHost *host = (__bridge SHRuntimeHost *)context;
  NSString *uuid = [NSString stringWithUTF8String:target.uuid.c_str()];
  if (uuid == nil) {
    return false;
  }

  // The runtime executes actions on the run loop it was started from (the
  // main thread), but guard anyway: AppKit work must happen on main.
  if (NSThread.isMainThread) {
    return [host activateEmptyDisplayWithUUID:uuid];
  }

  __block BOOL handled = NO;
  dispatch_sync(dispatch_get_main_queue(), ^{
    handled = [host activateEmptyDisplayWithUUID:uuid];
  });
  return handled;
}

- (instancetype)init {
  self = [super init];
  if (self == nil) {
    return nil;
  }

  _statusText = @"Stopped";
  _menuBarTitle = @"";

  // The anchor only needs to exist until something else takes focus.
  NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
  [center addObserver:self
             selector:@selector(applicationDidResignActive:)
                 name:NSApplicationDidResignActiveNotification
               object:nil];
  [center addObserver:self
             selector:@selector(windowDidBecomeKey:)
                 name:NSWindowDidBecomeKeyNotification
               object:nil];
  return self;
}

- (void)dealloc {
  [NSNotificationCenter.defaultCenter removeObserver:self];
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
  options.delegate.activate_empty_display = SHRuntimeHostActivateEmptyDisplay;
  options.delegate.context = (__bridge void *)self;

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

- (BOOL)activateEmptyDisplayWithUUID:(NSString *)uuid {
  NSScreen *screen = screen_for_display_uuid(uuid);
  if (screen == nil) {
    os_log_error(SHLogNavigation(), "Empty display activation failed (code=screen-not-found)");
    return NO;
  }

  if (self.displayAnchorWindow == nil) {
    SHDisplayAnchorWindow *window =
        [[SHDisplayAnchorWindow alloc] initWithContentRect:NSMakeRect(0.0, 0.0, 1.0, 1.0)
                                                 styleMask:NSWindowStyleMaskBorderless
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
    window.releasedWhenClosed = NO;
    window.alphaValue = 0.0;
    window.opaque = NO;
    window.hasShadow = NO;
    window.backgroundColor = NSColor.clearColor;
    window.ignoresMouseEvents = YES;
    window.excludedFromWindowsMenu = YES;
    window.animationBehavior = NSWindowAnimationBehaviorNone;
    window.collectionBehavior = NSWindowCollectionBehaviorMoveToActiveSpace |
                                NSWindowCollectionBehaviorStationary |
                                NSWindowCollectionBehaviorIgnoresCycle |
                                NSWindowCollectionBehaviorFullScreenAuxiliary;
    self.displayAnchorWindow = window;
  }

  const NSRect frame = screen.frame;
  [self.displayAnchorWindow setFrameOrigin:NSMakePoint(NSMidX(frame), NSMidY(frame))];
  // See SHDisplayAnchorWindow: only a Regular app's key window drives the
  // active menu-bar display. Reverted in -dismissDisplayAnchor.
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
  [self.displayAnchorWindow makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
  os_log_debug(SHLogNavigation(), "Empty display activation requested via anchor window");

  // Activation completes asynchronously; report (but don't retry) if macOS
  // declined so the failure is visible in the logs.
  __weak SHRuntimeHost *weakSelf = self;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(250 * NSEC_PER_MSEC)),
                 dispatch_get_main_queue(), ^{
    SHRuntimeHost *strongSelf = weakSelf;
    if (strongSelf == nil || strongSelf.displayAnchorWindow == nil) {
      return;
    }
    if (!strongSelf.displayAnchorWindow.isKeyWindow) {
      os_log_error(SHLogNavigation(),
                   "Empty display activation was not honored (code=anchor-not-key active=%{public}d)",
                   NSApp.isActive);
    }
  });
  return YES;
}

- (void)dismissDisplayAnchor {
  if (self.displayAnchorWindow == nil || !self.displayAnchorWindow.isVisible) {
    return;
  }

  [self.displayAnchorWindow orderOut:nil];
  os_log_debug(SHLogNavigation(), "Display anchor window dismissed");

  // The Settings window also runs the app as Regular while it is open and
  // reverts on close; only drop back to Accessory when nothing else of ours
  // is showing.
  for (NSWindow *window in NSApp.windows) {
    if (window != self.displayAnchorWindow && window.isVisible) {
      return;
    }
  }
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

- (void)applicationDidResignActive:(NSNotification *)notification {
  (void)notification;
  [self dismissDisplayAnchor];
}

- (void)windowDidBecomeKey:(NSNotification *)notification {
  // One of our real windows (e.g. Settings) took focus; the anchor is no
  // longer needed.
  if (notification.object != self.displayAnchorWindow) {
    [self dismissDisplayAnchor];
  }
}

- (void)updateMenuBarTitle:(NSString *)menuBarTitle statusText:(NSString *)statusText {
  _menuBarTitle = [menuBarTitle copy];
  _statusText = [statusText copy];

  if (self.stateChangeHandler != nil) {
    self.stateChangeHandler(_menuBarTitle, _statusText);
  }
}

@end
