// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#import "AppDelegate.h"
#import "SHCrashReporting.h"
#import "SHLogging.h"
#import "SHPermissions.h"
#import "SHRuntimeHost.h"
#import "SHSentryMonitoring.h"
#import "SHSettingsWindowController.h"
#import "SHUpdateChannel.h"
#import <Sparkle/Sparkle.h>

@interface AppDelegate () <SPUUpdaterDelegate, NSMenuDelegate>

@property(nonatomic, strong) NSStatusItem *statusItem;
@property(nonatomic, strong) NSMenu *statusMenu;
@property(nonatomic, strong) NSMenuItem *runtimeErrorItem;
@property(nonatomic, strong) NSMenuItem *grantAccessItem;
@property(nonatomic, strong) NSMenuItem *testCrashItem;
@property(nonatomic, strong) SHRuntimeHost *runtimeHost;
@property(nonatomic, strong) SHSettingsWindowController *settingsWindowController;
@property(nonatomic, strong, nullable) NSTimer *accessibilityPollTimer;
@property(nonatomic, strong, nullable) SPUStandardUpdaterController *updaterController;

@end

// The menu bar font used for the Space number, with monospaced digits so the
// number keeps the same footprint no matter which digits it contains.
static NSFont *SHStatusItemFont(void) {
  CGFloat pointSize = [NSFont menuBarFontOfSize:0].pointSize;
  return [NSFont monospacedDigitSystemFontOfSize:pointSize weight:NSFontWeightRegular];
}

// A status item length that depends only on how many digits the title has, so
// the item hugs a single-digit Space number yet never resizes between two
// numbers of the same digit count. Titles that are not a Space number (empty,
// or replaced by the attention badge) get a standard square item.
static CGFloat SHStatusItemLengthForTitle(NSString *title) {
  if (title.length == 0) {
    return [NSStatusBar systemStatusBar].thickness;
  }
  static const CGFloat kHorizontalPadding = 8.0;
  NSString *widest = [@"" stringByPaddingToLength:title.length withString:@"8" startingAtIndex:0];
  NSDictionary *attributes = @{NSFontAttributeName : SHStatusItemFont()};
  CGFloat textWidth = ceil([widest sizeWithAttributes:attributes].width);
  return textWidth + kHorizontalPadding;
}

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;

  os_log_info(SHLogLifecycle(), "Application finished launching");
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
  [self installMainMenu];

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

  // The length is managed explicitly (see setMenuBarTitle:) so neighbouring
  // menu bar items don't shift as the Space number changes.
  self.statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
  NSStatusBarButton *button = self.statusItem.button;
  if (button == nil) {
    os_log_fault(SHLogLifecycle(), "Status bar button was unavailable; launch cannot continue");
    return;
  }

  button.toolTip = @"SpaceHound";
  button.image = nil;
  button.font = SHStatusItemFont();

  self.runtimeHost = [[SHRuntimeHost alloc] init];
  [self setMenuBarTitle:self.runtimeHost.menuBarTitle];
  self.statusMenu = [[NSMenu alloc] initWithTitle:@"SpaceHound"];
  self.statusMenu.delegate = self;

  NSMenuItem *titleItem = [[NSMenuItem alloc] initWithTitle:@"SpaceHound" action:nil keyEquivalent:@""];
  titleItem.enabled = NO;
  [self.statusMenu addItem:titleItem];

  // Revealed only while the runtime has failed to start; see enterRuntimeErrorState:.
  self.runtimeErrorItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
  self.runtimeErrorItem.enabled = NO;
  self.runtimeErrorItem.hidden = YES;
  [self.statusMenu addItem:self.runtimeErrorItem];

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
    NSMenuItem *checkForUpdatesItem =
        [[NSMenuItem alloc] initWithTitle:@"Check for Updates…"
                                   action:@selector(checkForUpdates:)
                            keyEquivalent:@""];
    checkForUpdatesItem.target = self.updaterController;
    [self.statusMenu addItem:checkForUpdatesItem];
  }

  // Revealed only while Option is held when the menu opens; see menuNeedsUpdate:.
  self.testCrashItem = [[NSMenuItem alloc] initWithTitle:@"Test Crash Reporting…"
                                                  action:@selector(testCrashReporting:)
                                           keyEquivalent:@""];
  self.testCrashItem.target = self;
  self.testCrashItem.hidden = YES;
  [self.statusMenu addItem:self.testCrashItem];

  [self.statusMenu addItem:[NSMenuItem separatorItem]];

  NSMenuItem *quitItem =
      [[NSMenuItem alloc] initWithTitle:@"Quit SpaceHound" action:@selector(quit:) keyEquivalent:@"q"];
  quitItem.target = self;
  [self.statusMenu addItem:quitItem];

  self.statusItem.menu = self.statusMenu;

  __weak typeof(self) weakSelf = self;
  self.runtimeHost.stateChangeHandler = ^(NSString *menuBarTitle, NSString *_Nullable startError) {
    [weakSelf setMenuBarTitle:menuBarTitle];
    if (startError != nil) {
      [weakSelf enterRuntimeErrorState:startError];
    } else if (!weakSelf.runtimeErrorItem.hidden) {
      [weakSelf enterReadyState];
    }
  };

  self.settingsWindowController = [[SHSettingsWindowController alloc] init];
  __weak typeof(self) weakWindowSelf = self;
  self.settingsWindowController.applyHandler = ^BOOL(NSError **error) {
    return [weakWindowSelf.runtimeHost applySettings:error];
  };
  self.settingsWindowController.inputSuspensionHandler = ^(BOOL suspended) {
    [weakWindowSelf.runtimeHost setInputSuspended:suspended];
  };
  self.settingsWindowController.updateChannelSelectable = (self.updaterController != nil);
  self.settingsWindowController.updateChannelChangedHandler = ^{
    // Re-evaluate the feed against the new channel set right away.
    [weakWindowSelf.updaterController.updater resetUpdateCycle];
  };
  self.settingsWindowController.crashReportingAvailable = SHCrashReportingIsAvailable();
  self.settingsWindowController.crashReportingChangedHandler = ^{
    if ([SHCrashReporting isEnabled]) {
      SHStartCrashReportingIfEnabled();
    } else {
      SHStopCrashReporting();
    }
  };

  // Ask before the Accessibility prompt so the privacy decision comes first.
  // Builds without a DSN have nothing to ask about.
  if (SHCrashReportingIsAvailable() && ![SHCrashReporting hasRecordedChoice]) {
    [self presentCrashReportingPrompt];
  }

  [self startRuntimeOrRequestAccess];
  os_log_info(SHLogLifecycle(), "Application launch setup completed");
}

// The app launches as a menu-bar agent, so this menu is invisible until the
// settings window switches the activation policy to Regular. It gives that
// window a proper menu bar: the app name, standard Edit shortcuts for text
// fields, and Cmd-W / Cmd-Q.
- (void)installMainMenu {
  NSMenu *mainMenu = [[NSMenu alloc] initWithTitle:@"MainMenu"];

  // The app menu's title is replaced by the process name at runtime.
  NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"SpaceHound"];
  [appMenu addItemWithTitle:@"About SpaceHound" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
  [appMenu addItem:[NSMenuItem separatorItem]];
  NSMenuItem *settingsItem =
      [[NSMenuItem alloc] initWithTitle:@"Settings…" action:@selector(openSettings:) keyEquivalent:@","];
  settingsItem.target = self;
  [appMenu addItem:settingsItem];
  [appMenu addItem:[NSMenuItem separatorItem]];
  [appMenu addItemWithTitle:@"Hide SpaceHound" action:@selector(hide:) keyEquivalent:@"h"];
  NSMenuItem *hideOthersItem =
      [appMenu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
  hideOthersItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
  [appMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
  [appMenu addItem:[NSMenuItem separatorItem]];
  NSMenuItem *quitItem =
      [[NSMenuItem alloc] initWithTitle:@"Quit SpaceHound" action:@selector(quit:) keyEquivalent:@"q"];
  quitItem.target = self;
  [appMenu addItem:quitItem];
  NSMenuItem *appMenuItem = [[NSMenuItem alloc] initWithTitle:@"SpaceHound" action:nil keyEquivalent:@""];
  appMenuItem.submenu = appMenu;
  [mainMenu addItem:appMenuItem];

  // Nil targets route through the first responder so text fields get them.
  NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Edit"];
  [editMenu addItemWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"];
  NSMenuItem *redoItem = [editMenu addItemWithTitle:@"Redo" action:@selector(redo:) keyEquivalent:@"z"];
  redoItem.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
  [editMenu addItem:[NSMenuItem separatorItem]];
  [editMenu addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
  [editMenu addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
  [editMenu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
  [editMenu addItemWithTitle:@"Delete" action:@selector(delete:) keyEquivalent:@""];
  [editMenu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
  NSMenuItem *editMenuItem = [[NSMenuItem alloc] initWithTitle:@"Edit" action:nil keyEquivalent:@""];
  editMenuItem.submenu = editMenu;
  [mainMenu addItem:editMenuItem];

  NSMenu *windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
  [windowMenu addItemWithTitle:@"Close" action:@selector(performClose:) keyEquivalent:@"w"];
  [windowMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
  [windowMenu addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
  NSMenuItem *windowMenuItem = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
  windowMenuItem.submenu = windowMenu;
  [mainMenu addItem:windowMenuItem];

  NSApp.mainMenu = mainMenu;
  NSApp.windowsMenu = windowMenu;
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

#pragma mark - Status menu

- (void)menuNeedsUpdate:(NSMenu *)menu {
  if (menu != self.statusMenu) {
    return;
  }
  const BOOL optionHeld = (NSEvent.modifierFlags & NSEventModifierFlagOption) != 0;
  self.testCrashItem.hidden = !optionHeld;
}

// The status menu auto-enables its items, so the test-crash item's state has
// to come from validation rather than a direct `enabled` assignment.
- (BOOL)validateMenuItem:(NSMenuItem *)menuItem {
  if (menuItem.action == @selector(testCrashReporting:)) {
    return SHCrashReportingIsAvailable() && [SHCrashReporting isEnabled];
  }
  return YES;
}

#pragma mark - Crash reporting

- (void)presentCrashReportingPrompt {
  [NSApp activateIgnoringOtherApps:YES];

  NSAlert *alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleInformational;
  alert.messageText = @"Help improve SpaceHound by sending crash reports?";
  alert.informativeText =
      @"When SpaceHound crashes, a report can be sent to Sentry so the problem can be fixed. "
      @"Reports contain the crash signal or exception, the stack trace, the SpaceHound version "
      @"and build, and the macOS version and Mac model.\n\n"
      @"Reports never include your settings, window titles, keystrokes, identity, or usage "
      @"analytics. You can change this at any time in Settings.";
  [alert addButtonWithTitle:@"Send Crash Reports"];
  [alert addButtonWithTitle:@"Don't Send"];

  const NSModalResponse response = [alert runModal];
  const BOOL enabled = (response == NSAlertFirstButtonReturn);
  [SHCrashReporting setEnabled:enabled];
  if (enabled) {
    os_log_info(SHLogCrashReporting(), "Crash reporting accepted at the launch prompt");
    SHStartCrashReportingIfEnabled();
  } else {
    os_log_info(SHLogCrashReporting(), "Crash reporting declined at the launch prompt");
  }
}

- (void)testCrashReporting:(id)sender {
  (void)sender;
  [NSApp activateIgnoringOtherApps:YES];

  NSAlert *alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleCritical;
  alert.messageText = @"Crash SpaceHound now?";
  alert.informativeText =
      @"SpaceHound will quit immediately with a deliberate crash. The report is uploaded to "
      @"Sentry the next time SpaceHound launches.\n\n"
      @"No report is captured while a debugger is attached.";
  [alert addButtonWithTitle:@"Crash Now"];
  [alert addButtonWithTitle:@"Cancel"];

  if ([alert runModal] != NSAlertFirstButtonReturn) {
    os_log_info(SHLogCrashReporting(), "Test crash cancelled");
    return;
  }
  // The runtime is left running on purpose: the crash should look like a real one.
  SHCrashForTesting();
}

#pragma mark - Update channel

- (NSSet<NSString *> *)allowedChannelsForUpdater:(SPUUpdater *)updater {
  (void)updater;
  // Production items carry no channel and are always eligible. Opting in adds
  // the beta channel, and Sparkle offers the highest version across both.
  return [SHUpdateChannel receivesBetaUpdates] ? [NSSet setWithObject:@"beta"] : [NSSet set];
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

// Sets the Space number shown in the menu bar and sizes the item to fit its
// digit count, so switching between Spaces with the same number of digits
// leaves the neighbouring menu bar items exactly where they were.
- (void)setMenuBarTitle:(NSString *)title {
  NSStatusBarButton *button = self.statusItem.button;
  if (button == nil) {
    return;
  }
  button.title = title;
  CGFloat length = SHStatusItemLengthForTitle(title);
  if (self.statusItem.length != length) {
    self.statusItem.length = length;
  }
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
    self.statusItem.length = NSSquareStatusItemLength;
  }
  self.grantAccessItem.hidden = NO;
}

// Shows the attention badge and reveals a menu item naming the start failure,
// so a broken settings file or runtime error is visible from the menu bar.
- (void)enterRuntimeErrorState:(NSString *)message {
  NSStatusBarButton *button = self.statusItem.button;
  if (button != nil) {
    NSImage *warning = [NSImage imageWithSystemSymbolName:@"exclamationmark.triangle"
                                 accessibilityDescription:message];
    warning.template = YES;
    button.image = warning;
    button.title = @"";
    self.statusItem.length = NSSquareStatusItemLength;
  }
  self.runtimeErrorItem.title = message;
  self.runtimeErrorItem.hidden = NO;
}

// Clears the attention badge and hides the grant and error items so the runtime
// can drive the menu bar title normally.
- (void)enterReadyState {
  NSStatusBarButton *button = self.statusItem.button;
  if (button != nil) {
    button.image = nil;
  }
  self.grantAccessItem.hidden = YES;
  self.runtimeErrorItem.hidden = YES;
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
