#import "AppDelegate.h"
#import "SRDaemonSupervisor.h"

@interface AppDelegate ()

@property(nonatomic, strong) NSStatusItem *statusItem;
@property(nonatomic, strong) NSMenu *statusMenu;
@property(nonatomic, strong) NSMenuItem *daemonStatusItem;
@property(nonatomic, strong) SRDaemonSupervisor *daemonSupervisor;

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

  NSImage *statusImage =
      [NSImage imageWithSystemSymbolName:@"square.grid.2x2" accessibilityDescription:@"SpaceRabbit"];
  if (statusImage != nil) {
    statusImage.template = YES;
    button.image = statusImage;
  } else {
    button.title = @"SR";
  }

  self.daemonSupervisor = [[SRDaemonSupervisor alloc] init];
  self.statusMenu = [[NSMenu alloc] initWithTitle:@"SpaceRabbit"];

  NSMenuItem *titleItem = [[NSMenuItem alloc] initWithTitle:@"SpaceRabbit" action:nil keyEquivalent:@""];
  titleItem.enabled = NO;
  [self.statusMenu addItem:titleItem];

  self.daemonStatusItem =
      [[NSMenuItem alloc] initWithTitle:self.daemonSupervisor.statusText action:nil keyEquivalent:@""];
  self.daemonStatusItem.enabled = NO;
  [self.statusMenu addItem:self.daemonStatusItem];

  NSMenuItem *restartItem =
      [[NSMenuItem alloc] initWithTitle:@"Restart Daemon" action:@selector(restartDaemon:) keyEquivalent:@"r"];
  restartItem.target = self;
  [self.statusMenu addItem:restartItem];

  [self.statusMenu addItem:[NSMenuItem separatorItem]];

  NSMenuItem *quitItem =
      [[NSMenuItem alloc] initWithTitle:@"Quit SpaceRabbit" action:@selector(quit:) keyEquivalent:@"q"];
  quitItem.target = self;
  [self.statusMenu addItem:quitItem];

  self.statusItem.menu = self.statusMenu;

  __weak typeof(self) weakSelf = self;
  self.daemonSupervisor.statusChangeHandler = ^(NSString *statusText) {
    weakSelf.daemonStatusItem.title = statusText;
  };
  [self.daemonSupervisor start];
}

- (void)applicationWillTerminate:(NSNotification *)notification {
  (void)notification;
  [self.daemonSupervisor stop];
}

- (void)restartDaemon:(id)sender {
  (void)sender;
  [self.daemonSupervisor restart];
}

- (void)quit:(id)sender {
  (void)sender;
  [self.daemonSupervisor stop];
  [NSApp terminate:nil];
}

@end
