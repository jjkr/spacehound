#import "AppDelegate.h"
#import "SRRuntimeHost.h"
#import "SRSettingsWindowController.h"

@interface AppDelegate ()

@property(nonatomic, strong) NSStatusItem *statusItem;
@property(nonatomic, strong) NSMenu *statusMenu;
@property(nonatomic, strong) NSMenuItem *runtimeStatusItem;
@property(nonatomic, strong) SRRuntimeHost *runtimeHost;
@property(nonatomic, strong) SRSettingsWindowController *settingsWindowController;

@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;

  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

  self.statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
  NSStatusBarButton *button = self.statusItem.button;
  if (button == nil) {
    return;
  }

  button.toolTip = @"SpaceRabbit";
  button.image = nil;

  self.runtimeHost = [[SRRuntimeHost alloc] init];
  button.title = self.runtimeHost.menuBarTitle;
  self.statusMenu = [[NSMenu alloc] initWithTitle:@"SpaceRabbit"];

  NSMenuItem *titleItem = [[NSMenuItem alloc] initWithTitle:@"SpaceRabbit" action:nil keyEquivalent:@""];
  titleItem.enabled = NO;
  [self.statusMenu addItem:titleItem];

  self.runtimeStatusItem =
      [[NSMenuItem alloc] initWithTitle:self.runtimeHost.statusText action:nil keyEquivalent:@""];
  self.runtimeStatusItem.enabled = NO;
  [self.statusMenu addItem:self.runtimeStatusItem];

  [self.statusMenu addItem:[NSMenuItem separatorItem]];

  NSMenuItem *settingsItem =
      [[NSMenuItem alloc] initWithTitle:@"Settings..." action:@selector(openSettings:) keyEquivalent:@","];
  settingsItem.target = self;
  [self.statusMenu addItem:settingsItem];

  NSMenuItem *quitItem =
      [[NSMenuItem alloc] initWithTitle:@"Quit SpaceRabbit" action:@selector(quit:) keyEquivalent:@"q"];
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

  self.settingsWindowController = [[SRSettingsWindowController alloc] init];
  __weak typeof(self) weakWindowSelf = self;
  self.settingsWindowController.applyHandler = ^BOOL(NSError **error) {
    return [weakWindowSelf.runtimeHost applySettings:error];
  };

  [self.runtimeHost start];
}

- (void)applicationWillTerminate:(NSNotification *)notification {
  (void)notification;
  [self.runtimeHost stop];
}

- (void)quit:(id)sender {
  (void)sender;
  [self.runtimeHost stop];
  [NSApp terminate:nil];
}

- (void)openSettings:(id)sender {
  (void)sender;
  [self.settingsWindowController showWindowAndActivate];
}

@end
