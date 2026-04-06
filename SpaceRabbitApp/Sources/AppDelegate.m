#import "AppDelegate.h"

@interface AppDelegate ()

@property(nonatomic, strong) NSStatusItem *statusItem;
@property(nonatomic, strong) NSMenu *statusMenu;

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

  self.statusMenu = [[NSMenu alloc] initWithTitle:@"SpaceRabbit"];

  NSMenuItem *titleItem = [[NSMenuItem alloc] initWithTitle:@"SpaceRabbit" action:nil keyEquivalent:@""];
  titleItem.enabled = NO;
  [self.statusMenu addItem:titleItem];
  [self.statusMenu addItem:[NSMenuItem separatorItem]];

  NSMenuItem *quitItem =
      [[NSMenuItem alloc] initWithTitle:@"Quit SpaceRabbit" action:@selector(quit:) keyEquivalent:@"q"];
  quitItem.target = self;
  [self.statusMenu addItem:quitItem];

  self.statusItem.menu = self.statusMenu;
}

- (void)quit:(id)sender {
  (void)sender;
  [NSApp terminate:nil];
}

@end
