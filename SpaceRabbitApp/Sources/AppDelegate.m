#import "AppDelegate.h"
#import "SRRuntimeHost.h"

@interface AppDelegate ()

@property(nonatomic, strong) NSStatusItem *statusItem;
@property(nonatomic, strong) NSMenu *statusMenu;
@property(nonatomic, strong) NSMenuItem *runtimeStatusItem;
@property(nonatomic, strong) SRRuntimeHost *runtimeHost;

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

  self.runtimeHost = [[SRRuntimeHost alloc] init];
  self.statusMenu = [[NSMenu alloc] initWithTitle:@"SpaceRabbit"];

  NSMenuItem *titleItem = [[NSMenuItem alloc] initWithTitle:@"SpaceRabbit" action:nil keyEquivalent:@""];
  titleItem.enabled = NO;
  [self.statusMenu addItem:titleItem];

  self.runtimeStatusItem =
      [[NSMenuItem alloc] initWithTitle:self.runtimeHost.statusText action:nil keyEquivalent:@""];
  self.runtimeStatusItem.enabled = NO;
  [self.statusMenu addItem:self.runtimeStatusItem];

  [self.statusMenu addItem:[NSMenuItem separatorItem]];

  NSMenuItem *quitItem =
      [[NSMenuItem alloc] initWithTitle:@"Quit SpaceRabbit" action:@selector(quit:) keyEquivalent:@"q"];
  quitItem.target = self;
  [self.statusMenu addItem:quitItem];

  self.statusItem.menu = self.statusMenu;

  __weak typeof(self) weakSelf = self;
  self.runtimeHost.statusChangeHandler = ^(NSString *statusText) {
    weakSelf.runtimeStatusItem.title = statusText;
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

@end
