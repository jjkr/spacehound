#import "AppDelegate.h"
#import "SHLogging.h"
#import "SHPermissions.h"
#import "SHRuntimeHost.h"
#import "SHSettingsWindowController.h"
#import <Sparkle/Sparkle.h>

static NSString *const SHReceiveBetaUpdatesKey = @"SHReceiveBetaUpdates";

@interface AppDelegate () <SPUUpdaterDelegate>

@property(nonatomic, strong) NSStatusItem *statusItem;
@property(nonatomic, strong) NSMenu *statusMenu;
@property(nonatomic, strong) NSMenuItem *runtimeStatusItem;
@property(nonatomic, strong) NSMenuItem *grantAccessItem;
@property(nonatomic, strong) SHRuntimeHost *runtimeHost;
@property(nonatomic, strong) SHSettingsWindowController *settingsWindowController;
@property(nonatomic, strong, nullable) NSTimer *accessibilityPollTimer;
@property(nonatomic, strong, nullable) SPUStandardUpdaterController *updaterController;
@property(nonatomic, strong, nullable) NSMenuItem *receiveBetaUpdatesItem;

@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;

  os_log_info(SHLogLifecycle(), "Application finished launching");
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

  // Start the updater independently of Accessibility permission and the
  // SpaceHound runtime. This also lets users update a misconfigured install.
  NSString *sparklePublicKey = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"SUPublicEDKey"];
  if (sparklePublicKey.length > 0) {
    self.updaterController =
        [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES
                                                     updaterDelegate:self
                                                  userDriverDelegate:nil];
    os_log_info(SHLogUpdates(), "Automatic update service started");
  } else {
    os_log_info(SHLogUpdates(), "Automatic update service disabled because no public key is configured");
  }

  self.statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
  NSStatusBarButton *button = self.statusItem.button;
  if (button == nil) {
    os_log_fault(SHLogLifecycle(), "Status bar button was unavailable; launch cannot continue");
    return;
  }

  button.toolTip = @"SpaceHound";
  button.image = nil;

  self.runtimeHost = [[SHRuntimeHost alloc] init];
  button.title = self.runtimeHost.menuBarTitle;
  self.statusMenu = [[NSMenu alloc] initWithTitle:@"SpaceHound"];

  NSMenuItem *titleItem = [[NSMenuItem alloc] initWithTitle:@"SpaceHound" action:nil keyEquivalent:@""];
  titleItem.enabled = NO;
  [self.statusMenu addItem:titleItem];

  self.runtimeStatusItem =
      [[NSMenuItem alloc] initWithTitle:self.runtimeHost.statusText action:nil keyEquivalent:@""];
  self.runtimeStatusItem.enabled = NO;
  [self.statusMenu addItem:self.runtimeStatusItem];

  self.grantAccessItem = [[NSMenuItem alloc] initWithTitle:@"Grant Accessibility Access…"
                                                    action:@selector(grantAccess:)
                                             keyEquivalent:@""];
  self.grantAccessItem.target = self;
  self.grantAccessItem.hidden = YES;
  [self.statusMenu addItem:self.grantAccessItem];

  [self.statusMenu addItem:[NSMenuItem separatorItem]];

  NSMenuItem *settingsItem =
      [[NSMenuItem alloc] initWithTitle:@"Settings..." action:@selector(openSettings:) keyEquivalent:@","];
  settingsItem.target = self;
  [self.statusMenu addItem:settingsItem];

  if (self.updaterController != nil) {
    self.receiveBetaUpdatesItem =
        [[NSMenuItem alloc] initWithTitle:@"Receive Beta Updates"
                                  action:@selector(toggleBetaUpdates:)
                           keyEquivalent:@""];
    self.receiveBetaUpdatesItem.target = self;
    self.receiveBetaUpdatesItem.state =
        [self receivesBetaUpdates] ? NSControlStateValueOn : NSControlStateValueOff;
    [self.statusMenu addItem:self.receiveBetaUpdatesItem];

    NSMenuItem *checkForUpdatesItem =
        [[NSMenuItem alloc] initWithTitle:@"Check for Updates…"
                                   action:@selector(checkForUpdates:)
                            keyEquivalent:@""];
    checkForUpdatesItem.target = self.updaterController;
    [self.statusMenu addItem:checkForUpdatesItem];
  }

  [self.statusMenu addItem:[NSMenuItem separatorItem]];

  NSMenuItem *quitItem =
      [[NSMenuItem alloc] initWithTitle:@"Quit SpaceHound" action:@selector(quit:) keyEquivalent:@"q"];
  quitItem.target = self;
  [self.statusMenu addItem:quitItem];

  self.statusItem.menu = self.statusMenu;

  __weak typeof(self) weakSelf = self;
  self.runtimeHost.stateChangeHandler = ^(NSString *menuBarTitle, NSString *statusText) {
    NSStatusBarButton *strongButton = weakSelf.statusItem.button;
    if (strongButton != nil) {
      strongButton.title = menuBarTitle;
    }
    weakSelf.runtimeStatusItem.title = statusText;
  };

  self.settingsWindowController = [[SHSettingsWindowController alloc] init];
  __weak typeof(self) weakWindowSelf = self;
  self.settingsWindowController.applyHandler = ^BOOL(NSError **error) {
    return [weakWindowSelf.runtimeHost applySettings:error];
  };
  self.settingsWindowController.inputSuspensionHandler = ^(BOOL suspended) {
    [weakWindowSelf.runtimeHost setInputSuspended:suspended];
  };

  [self startRuntimeOrRequestAccess];
  os_log_info(SHLogLifecycle(), "Application launch setup completed");
}

- (void)applicationWillTerminate:(NSNotification *)notification {
  (void)notification;
  os_log_info(SHLogLifecycle(), "Application will terminate");
  [self.accessibilityPollTimer invalidate];
  self.accessibilityPollTimer = nil;
  [self.runtimeHost stop];
}

- (void)quit:(id)sender {
  (void)sender;
  os_log_info(SHLogLifecycle(), "Quit requested from the status menu");
  [self.runtimeHost stop];
  [NSApp terminate:nil];
}

- (void)openSettings:(id)sender {
  (void)sender;
  os_log_info(SHLogSettings(), "Settings window requested");
  [self.settingsWindowController showWindowAndActivate];
}

#pragma mark - Update channel

- (BOOL)receivesBetaUpdates {
  return [[NSUserDefaults standardUserDefaults] boolForKey:SHReceiveBetaUpdatesKey];
}

- (nullable NSString *)feedURLStringForUpdater:(SPUUpdater *)updater {
  (void)updater;
  if (![self receivesBetaUpdates]) {
    // nil tells Sparkle to use the production SUFeedURL from Info.plist.
    return nil;
  }

  NSString *betaFeedURL = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"SUBetaFeedURL"];
  return betaFeedURL.length > 0 ? betaFeedURL : nil;
}

- (void)toggleBetaUpdates:(id)sender {
  (void)sender;
  const BOOL enabling = ![self receivesBetaUpdates];
  if (enabling) {
    [NSApp activateIgnoringOtherApps:YES];
    NSAlert *alert = [[NSAlert alloc] init];
    alert.alertStyle = NSAlertStyleWarning;
    alert.messageText = @"Receive beta updates?";
    alert.informativeText =
        @"Beta updates are signed and notarized like production releases, but may contain "
         "unfinished changes. You can return to production updates from this menu at any time.";
    [alert addButtonWithTitle:@"Receive Beta Updates"];
    [alert addButtonWithTitle:@"Cancel"];
    if ([alert runModal] != NSAlertFirstButtonReturn) {
      os_log_info(SHLogUpdates(), "Beta update opt-in cancelled");
      return;
    }
  }

  [[NSUserDefaults standardUserDefaults] setBool:enabling forKey:SHReceiveBetaUpdatesKey];
  self.receiveBetaUpdatesItem.state = enabling ? NSControlStateValueOn : NSControlStateValueOff;
  [self.updaterController.updater resetUpdateCycle];
  if (enabling) {
    os_log_info(SHLogUpdates(), "Beta update channel enabled");
  } else {
    os_log_info(SHLogUpdates(), "Beta update channel disabled");
  }
}

- (void)grantAccess:(id)sender {
  (void)sender;
  if ([SHPermissions hasAccessibilityAccess]) {
    os_log_info(SHLogPermissions(), "Accessibility access already granted");
    [self enterReadyState];
    [self.runtimeHost start];
    return;
  }
  [self beginRequestingAccessibilityAccess];
}

#pragma mark - Accessibility permission workflow

// Starts the runtime when Accessibility access is already granted; otherwise
// surfaces the "needs access" state and walks the user through granting it.
- (void)startRuntimeOrRequestAccess {
  if ([SHPermissions hasAccessibilityAccess]) {
    os_log_info(SHLogPermissions(), "Accessibility access granted at launch");
    [self enterReadyState];
    [self.runtimeHost start];
    return;
  }

  os_log_info(SHLogPermissions(), "Accessibility access is required");
  [self enterNeedsAccessibilityState];
  [self presentAccessibilityPrompt];
}

// Shows an attention badge in the menu bar instead of a blank icon and reveals
// the "Grant Accessibility Access…" menu item.
- (void)enterNeedsAccessibilityState {
  NSStatusBarButton *button = self.statusItem.button;
  if (button != nil) {
    NSImage *warning =
        [NSImage imageWithSystemSymbolName:@"exclamationmark.triangle"
                 accessibilityDescription:@"Accessibility access required"];
    warning.template = YES;
    button.image = warning;
    button.title = @"";
    button.toolTip = @"SpaceHound — Accessibility access required";
  }
  self.runtimeStatusItem.title = @"Accessibility access required";
  self.grantAccessItem.hidden = NO;
}

// Clears the attention badge and hides the grant item so the runtime can drive
// the menu bar title normally.
- (void)enterReadyState {
  NSStatusBarButton *button = self.statusItem.button;
  if (button != nil) {
    button.image = nil;
    button.toolTip = @"SpaceHound";
  }
  self.grantAccessItem.hidden = YES;
}

- (void)presentAccessibilityPrompt {
  [NSApp activateIgnoringOtherApps:YES];

  NSAlert *alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleInformational;
  alert.messageText = @"Allow SpaceHound to control your Mac";
  alert.informativeText =
      @"SpaceHound needs Accessibility access to switch Spaces and manage windows.\n\n"
      @"Open System Settings, then turn on SpaceHound under Privacy & Security > "
      @"Accessibility. SpaceHound starts automatically once access is granted.";
  [alert addButtonWithTitle:@"Open System Settings"];
  [alert addButtonWithTitle:@"Not Now"];
  [alert addButtonWithTitle:@"Quit SpaceHound"];

  const NSModalResponse response = [alert runModal];
  if (response == NSAlertFirstButtonReturn) {
    os_log_info(SHLogPermissions(), "Accessibility settings requested from the launch prompt");
    [self beginRequestingAccessibilityAccess];
  } else if (response == NSAlertThirdButtonReturn) {
    os_log_info(SHLogPermissions(), "Quit selected from the accessibility prompt");
    [NSApp terminate:nil];
  } else {
    os_log_info(SHLogPermissions(), "Accessibility prompt deferred");
    // "Not Now": leave the badge up and keep watching so the runtime starts on
    // its own if the user grants access from System Settings or the menu.
    [self startAccessibilityPolling];
  }
}

- (void)beginRequestingAccessibilityAccess {
  // Open System Settings directly rather than calling the AX "prompt" API — the
  // latter triggers a second, redundant macOS dialog on top of our own. The app
  // is already registered in the Accessibility list by our AXIsProcessTrusted()
  // checks, so it appears in the list ready to toggle.
  [SHPermissions openAccessibilitySettings];
  [self startAccessibilityPolling];
}

// Watches for Accessibility access being granted and starts the runtime the
// moment it is, so the user never has to relaunch the app.
- (void)startAccessibilityPolling {
  if (self.accessibilityPollTimer != nil) {
    return;
  }

  os_log_debug(SHLogPermissions(), "Started polling for Accessibility authorization");
  __weak typeof(self) weakSelf = self;
  self.accessibilityPollTimer =
      [NSTimer scheduledTimerWithTimeInterval:1.0
                                      repeats:YES
                                        block:^(NSTimer *timer) {
                                          if (![SHPermissions hasAccessibilityAccess]) {
                                            return;
                                          }
                                          [timer invalidate];
                                          weakSelf.accessibilityPollTimer = nil;
                                          os_log_info(SHLogPermissions(), "Accessibility access granted while running");
                                          [weakSelf enterReadyState];
                                          [weakSelf.runtimeHost start];
                                        }];
}

@end
