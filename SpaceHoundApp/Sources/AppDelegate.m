// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#import "AppDelegate.h"
#import "SHLogging.h"
#import "SHPermissions.h"
#import "SHRuntimeHost.h"
#import "SHSettingsWindowController.h"
#import "SHUpdateChannel.h"
#import <Sparkle/Sparkle.h>

@interface AppDelegate () <SPUUpdaterDelegate>

@property(nonatomic, strong) NSStatusItem *statusItem;
@property(nonatomic, strong) NSMenu *statusMenu;
@property(nonatomic, strong) NSMenuItem *runtimeStatusItem;
@property(nonatomic, strong) NSMenuItem *grantAccessItem;
@property(nonatomic, strong) SHRuntimeHost *runtimeHost;
@property(nonatomic, strong) SHSettingsWindowController *settingsWindowController;
@property(nonatomic, strong, nullable) NSTimer *accessibilityPollTimer;
@property(nonatomic, strong, nullable) SPUStandardUpdaterController *updaterController;

@end

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

  self.statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
  NSStatusBarButton *button = self.statusItem.button;
  if (button == nil) {
    os_log_fault(SHLogLifecycle(), "Status bar button was unavailable; launch cannot continue");
    return;
  }

  button.toolTip = @"SpaceHound";
  button.image = [self menuBarIcon];

  self.runtimeHost = [[SHRuntimeHost alloc] init];
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
  self.runtimeHost.stateChangeHandler = ^(NSString *statusText) {
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
  self.settingsWindowController.updateChannelSelectable = (self.updaterController != nil);
  self.settingsWindowController.updateChannelChangedHandler = ^{
    // Re-evaluate the feed against the new channel set right away.
    [weakWindowSelf.updaterController.updater resetUpdateCycle];
  };

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

// The template glyph shown in the menu bar while the app is running normally.
- (NSImage *)menuBarIcon {
  NSImage *icon = [NSImage imageWithSystemSymbolName:@"dog" accessibilityDescription:@"SpaceHound"];
  icon.template = YES;
  return icon;
}

// Shows an attention badge in the menu bar instead of the app icon and reveals
// the "Grant Accessibility Access…" menu item.
- (void)enterNeedsAccessibilityState {
  NSStatusBarButton *button = self.statusItem.button;
  if (button != nil) {
    NSImage *warning =
        [NSImage imageWithSystemSymbolName:@"exclamationmark.triangle"
                 accessibilityDescription:@"Accessibility access required"];
    warning.template = YES;
    button.image = warning;
    button.toolTip = @"SpaceHound — Accessibility access required";
  }
  self.runtimeStatusItem.title = @"Accessibility access required";
  self.grantAccessItem.hidden = NO;
}

// Restores the app icon in place of the attention badge and hides the grant
// item.
- (void)enterReadyState {
  NSStatusBarButton *button = self.statusItem.button;
  if (button != nil) {
    button.image = [self menuBarIcon];
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
