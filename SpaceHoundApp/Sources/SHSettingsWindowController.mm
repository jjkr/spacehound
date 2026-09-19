#import "SHSettingsWindowController.h"

#import "SHLoginItemManager.h"
#import "SHLogging.h"
#import "SHSettingsStore.h"
#import "SHUpdateChannel.h"

#pragma mark - Shortcut vocabulary helpers

namespace {

constexpr CGFloat kRecorderWidth = 176.0;
constexpr CGFloat kRecorderHeight = 24.0;
constexpr CGFloat kRowInsetX = 14.0;
constexpr CGFloat kRowInsetY = 9.0;

// Modifier flags -> canonical modifier names in a stable display order
// (Control, Option, Shift, Command — the native macOS ordering).
NSArray<NSString *> *SHModifiersFromFlags(NSEventModifierFlags flags) {
  NSMutableArray<NSString *> *modifiers = [NSMutableArray array];
  if (flags & NSEventModifierFlagControl) {
    [modifiers addObject:@"ctrl"];
  }
  if (flags & NSEventModifierFlagOption) {
    [modifiers addObject:@"option"];
  }
  if (flags & NSEventModifierFlagShift) {
    [modifiers addObject:@"shift"];
  }
  if (flags & NSEventModifierFlagCommand) {
    [modifiers addObject:@"cmd"];
  }
  return modifiers;
}

// Parses free-text modifier strings (as persisted on disk) into canonical,
// de-duplicated, consistently ordered modifier names.
NSArray<NSString *> *SHParseModifiers(NSString *text) {
  NSEventModifierFlags flags = 0;
  NSCharacterSet *separators = [NSCharacterSet characterSetWithCharactersInString:@",+"];
  for (NSString *raw in [text componentsSeparatedByCharactersInSet:separators]) {
    NSString *candidate =
        [[raw stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]] lowercaseString];
    if ([candidate isEqualToString:@"ctrl"] || [candidate isEqualToString:@"control"]) {
      flags |= NSEventModifierFlagControl;
    } else if ([candidate isEqualToString:@"option"] || [candidate isEqualToString:@"alt"]) {
      flags |= NSEventModifierFlagOption;
    } else if ([candidate isEqualToString:@"cmd"] || [candidate isEqualToString:@"command"] ||
               [candidate isEqualToString:@"meta"]) {
      flags |= NSEventModifierFlagCommand;
    } else if ([candidate isEqualToString:@"shift"]) {
      flags |= NSEventModifierFlagShift;
    }
  }
  return SHModifiersFromFlags(flags);
}

NSString *SHSymbolForModifier(NSString *modifier) {
  if ([modifier isEqualToString:@"ctrl"]) {
    return @"⌃";  // ⌃
  }
  if ([modifier isEqualToString:@"option"]) {
    return @"⌥";  // ⌥
  }
  if ([modifier isEqualToString:@"shift"]) {
    return @"⇧";  // ⇧
  }
  if ([modifier isEqualToString:@"cmd"]) {
    return @"⌘";  // ⌘
  }
  return @"";
}

// Maps a key event to a canonical key name, or nil for pure-modifier / unmappable
// events. Special keys are matched by virtual key code (layout independent);
// letters and digits fall back to the produced characters.
NSString *SHKeyNameForEvent(NSEvent *event) {
  switch (event.keyCode) {
    case 48:
      return @"Tab";
    case 49:
      return @"space";
    case 36:
    case 76:
      return @"return";
    case 53:
      return @"escape";
    case 51:
    case 117:
      return @"delete";
    case 123:
      return @"left";
    case 124:
      return @"right";
    case 125:
      return @"down";
    case 126:
      return @"up";
    case 33:
      return @"[";
    case 30:
      return @"]";
    case 27:
      return @"-";
    case 24:
      return @"=";
    default:
      break;
  }

  NSString *characters = event.charactersIgnoringModifiers;
  if (characters.length == 0) {
    return nil;
  }
  unichar first = [characters characterAtIndex:0];
  if (first >= 'a' && first <= 'z') {
    return [characters substringToIndex:1];
  }
  if (first >= 'A' && first <= 'Z') {
    return [[characters substringToIndex:1] lowercaseString];
  }
  if (first >= '0' && first <= '9') {
    return [characters substringToIndex:1];
  }
  if (first == '[' || first == ']' || first == '-' || first == '=') {
    return [characters substringToIndex:1];
  }
  return nil;
}

NSString *SHGlyphForKey(NSString *key) {
  if (key.length == 0) {
    return @"";
  }
  static NSDictionary<NSString *, NSString *> *glyphs = nil;
  static dispatch_once_t onceToken;
  dispatch_once(&onceToken, ^{
    glyphs = @{
      @"tab" : @"⇥",        // ⇥
      @"space" : @"␣",      // ␣
      @"return" : @"↩",     // ↩
      @"enter" : @"↩",      // ↩
      @"escape" : @"⎋",     // ⎋
      @"esc" : @"⎋",        // ⎋
      @"delete" : @"⌫",     // ⌫
      @"backspace" : @"⌫",  // ⌫
      @"left" : @"←",       // ←
      @"right" : @"→",      // →
      @"up" : @"↑",         // ↑
      @"down" : @"↓",       // ↓
    };
  });
  NSString *glyph = glyphs[key.lowercaseString];
  if (glyph != nil) {
    return glyph;
  }
  return key.uppercaseString;
}

NSString *SHDisplayString(NSArray<NSString *> *modifiers, NSString *key) {
  NSMutableString *result = [NSMutableString string];
  for (NSString *modifier in modifiers) {
    [result appendString:SHSymbolForModifier(modifier)];
  }
  [result appendString:SHGlyphForKey(key)];
  return result;
}

}  // namespace

#pragma mark - SHCardView

// A rounded, hairline-bordered container that follows the current appearance,
// used to group related rows the way System Settings does.
@interface SHCardView : NSView
@end

@implementation SHCardView

- (instancetype)initWithFrame:(NSRect)frameRect {
  self = [super initWithFrame:frameRect];
  if (self != nil) {
    self.wantsLayer = YES;
    self.translatesAutoresizingMaskIntoConstraints = NO;
  }
  return self;
}

- (BOOL)wantsUpdateLayer {
  return YES;
}

- (void)updateLayer {
  __weak SHCardView *weakSelf = self;
  [self.effectiveAppearance performAsCurrentDrawingAppearance:^{
    SHCardView *strongSelf = weakSelf;
    if (strongSelf == nil) {
      return;
    }
    strongSelf.layer.cornerRadius = 10.0;
    strongSelf.layer.backgroundColor = [NSColor controlBackgroundColor].CGColor;
    strongSelf.layer.borderColor = [NSColor separatorColor].CGColor;
    strongSelf.layer.borderWidth = 1.0;
  }];
}

- (void)viewDidChangeEffectiveAppearance {
  [super viewDidChangeEffectiveAppearance];
  self.needsDisplay = YES;
}

@end

#pragma mark - SHShortcutRecorderView

// Click-to-record shortcut control. Captures the next key combination the user
// presses and renders it with native modifier/key glyphs.
@interface SHShortcutRecorderView : NSView

@property(nonatomic, copy) NSString *keyString;
@property(nonatomic, copy) NSArray<NSString *> *modifiers;
@property(nonatomic, assign) BOOL activeAppearance;
@property(nonatomic, copy) void (^onChange)(void);
@property(nonatomic, copy) void (^onRecordingChanged)(BOOL recording);

- (void)setShortcutKey:(NSString *)key modifiers:(NSArray<NSString *> *)modifiers;
- (NSString *)modifiersText;

@end

@implementation SHShortcutRecorderView {
  NSTextField *_label;
  NSButton *_clearButton;
  BOOL _recording;
  NSEventModifierFlags _previewFlags;
}

- (instancetype)initWithFrame:(NSRect)frameRect {
  self = [super initWithFrame:frameRect];
  if (self == nil) {
    return nil;
  }

  _keyString = @"";
  _modifiers = @[];
  _activeAppearance = YES;
  self.translatesAutoresizingMaskIntoConstraints = NO;
  self.focusRingType = NSFocusRingTypeNone;
  self.toolTip = @"Click to record a shortcut";

  _label = [NSTextField labelWithString:@""];
  _label.translatesAutoresizingMaskIntoConstraints = NO;
  _label.alignment = NSTextAlignmentCenter;
  _label.font = [NSFont systemFontOfSize:13.0 weight:NSFontWeightMedium];

  _clearButton = [NSButton buttonWithImage:[self clearImage] target:self action:@selector(clearShortcut:)];
  _clearButton.translatesAutoresizingMaskIntoConstraints = NO;
  _clearButton.bordered = NO;
  _clearButton.bezelStyle = NSBezelStyleRegularSquare;
  _clearButton.imagePosition = NSImageOnly;
  _clearButton.contentTintColor = [NSColor tertiaryLabelColor];
  _clearButton.toolTip = @"Clear shortcut";
  [_clearButton setButtonType:NSButtonTypeMomentaryChange];

  [self addSubview:_label];
  [self addSubview:_clearButton];

  [NSLayoutConstraint activateConstraints:@[
    [self.widthAnchor constraintGreaterThanOrEqualToConstant:kRecorderWidth],
    [self.heightAnchor constraintEqualToConstant:kRecorderHeight],
    [_label.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:8.0],
    [_label.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-8.0],
    [_label.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
    [_clearButton.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-5.0],
    [_clearButton.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
    [_clearButton.widthAnchor constraintEqualToConstant:15.0],
    [_clearButton.heightAnchor constraintEqualToConstant:15.0],
  ]];

  [self refresh];
  return self;
}

- (NSImage *)clearImage {
  NSImage *image = [NSImage imageWithSystemSymbolName:@"xmark.circle.fill"
                              accessibilityDescription:@"Clear shortcut"];
  return image;
}

- (void)setShortcutKey:(NSString *)key modifiers:(NSArray<NSString *> *)modifiers {
  _keyString = [key copy] ?: @"";
  _modifiers = [modifiers copy] ?: @[];
  [self refresh];
}

- (void)setActiveAppearance:(BOOL)activeAppearance {
  _activeAppearance = activeAppearance;
  [self refresh];
}

- (NSString *)modifiersText {
  return [self.modifiers componentsJoinedByString:@", "];
}

- (BOOL)acceptsFirstResponder {
  return YES;
}

// Focus alone must not start recording — otherwise the window picks the first
// recorder as its initial first responder and it opens mid-capture. Recording
// begins only on an explicit click (or ends when focus is lost).
- (BOOL)becomeFirstResponder {
  return YES;
}

- (BOOL)resignFirstResponder {
  if (_recording) {
    _recording = NO;
    [self refresh];
    self.needsDisplay = YES;
    if (self.onRecordingChanged != nil) {
      self.onRecordingChanged(NO);
    }
  }
  return YES;
}

- (void)mouseDown:(NSEvent *)event {
  (void)event;
  [self beginRecording];
}

- (void)beginRecording {
  if (_recording) {
    return;
  }
  if (self.window.firstResponder != self) {
    [self.window makeFirstResponder:self];
  }
  _recording = YES;
  _previewFlags = 0;
  [self refresh];
  self.needsDisplay = YES;
  if (self.onRecordingChanged != nil) {
    self.onRecordingChanged(YES);
  }
}

- (NSView *)hitTest:(NSPoint)point {
  NSView *hit = [super hitTest:point];
  if (hit == nil) {
    return nil;
  }
  if (hit == _clearButton) {
    return _clearButton;
  }
  return self;
}

- (BOOL)performKeyEquivalent:(NSEvent *)event {
  if (_recording && self.window.firstResponder == self) {
    if ([self captureEvent:event]) {
      return YES;
    }
  }
  return [super performKeyEquivalent:event];
}

- (void)keyDown:(NSEvent *)event {
  if (_recording) {
    if ([self captureEvent:event]) {
      return;
    }
  }
  [super keyDown:event];
}

- (void)flagsChanged:(NSEvent *)event {
  if (_recording) {
    _previewFlags = event.modifierFlags &
                    (NSEventModifierFlagControl | NSEventModifierFlagOption | NSEventModifierFlagShift |
                     NSEventModifierFlagCommand);
    [self refresh];
    self.needsDisplay = YES;
  }
  [super flagsChanged:event];
}

- (BOOL)captureEvent:(NSEvent *)event {
  NSEventModifierFlags flags =
      event.modifierFlags & (NSEventModifierFlagControl | NSEventModifierFlagOption | NSEventModifierFlagShift |
                             NSEventModifierFlagCommand);

  // Escape with no modifiers cancels recording without changing the binding.
  if (event.keyCode == 53 && flags == 0) {
    [self.window makeFirstResponder:nil];
    return YES;
  }

  NSString *keyName = SHKeyNameForEvent(event);
  if (keyName == nil) {
    return NO;
  }

  _keyString = [keyName copy];
  _modifiers = SHModifiersFromFlags(flags);
  [self.window makeFirstResponder:nil];
  [self refresh];
  self.needsDisplay = YES;
  if (self.onChange != nil) {
    self.onChange();
  }
  return YES;
}

- (void)clearShortcut:(id)sender {
  (void)sender;
  _keyString = @"";
  _modifiers = @[];
  [self refresh];
  self.needsDisplay = YES;
  if (self.onChange != nil) {
    self.onChange();
  }
}

- (void)refresh {
  if (_label == nil) {
    return;
  }

  if (_recording) {
    NSString *preview = SHDisplayString(SHModifiersFromFlags(_previewFlags), @"");
    _label.stringValue = preview.length > 0 ? [preview stringByAppendingString:@"…"] : @"Type shortcut…";
    _label.textColor = [NSColor controlAccentColor];
    _clearButton.hidden = YES;
  } else if (self.keyString.length > 0) {
    _label.stringValue = SHDisplayString(self.modifiers, self.keyString);
    _label.textColor = [NSColor labelColor];
    _clearButton.hidden = NO;
  } else {
    _label.stringValue = @"Record Shortcut";
    _label.textColor = [NSColor tertiaryLabelColor];
    _clearButton.hidden = YES;
  }

  self.alphaValue = _activeAppearance ? 1.0 : 0.45;
}

- (void)viewDidChangeEffectiveAppearance {
  [super viewDidChangeEffectiveAppearance];
  self.needsDisplay = YES;
}

- (void)drawRect:(NSRect)dirtyRect {
  (void)dirtyRect;
  NSRect rect = NSInsetRect(self.bounds, 0.75, 0.75);
  NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:rect xRadius:6.0 yRadius:6.0];

  NSColor *fill = _recording ? [[NSColor controlAccentColor] colorWithAlphaComponent:0.12]
                             : [NSColor textBackgroundColor];
  [fill setFill];
  [path fill];

  path.lineWidth = _recording ? 2.0 : 1.0;
  NSColor *stroke = _recording ? [NSColor controlAccentColor] : [NSColor separatorColor];
  [stroke setStroke];
  [path stroke];
}

@end

#pragma mark - SHHotkeyRowView

@interface SHHotkeyRowView : NSView

@property(nonatomic, strong, readonly) SHHotkeyItem *item;

- (instancetype)initWithHotkeyItem:(SHHotkeyItem *)item
                  recordingChanged:(void (^)(BOOL recording))recordingChanged;
- (SHHotkeyItem *)currentItem;

@end

@implementation SHHotkeyRowView {
  NSSwitch *_enabledSwitch;
  SHShortcutRecorderView *_recorder;
}

- (instancetype)initWithHotkeyItem:(SHHotkeyItem *)item
                  recordingChanged:(void (^)(BOOL recording))recordingChanged {
  self = [super initWithFrame:NSZeroRect];
  if (self == nil) {
    return nil;
  }

  _item = item;
  self.translatesAutoresizingMaskIntoConstraints = NO;

  NSTextField *actionLabel = [NSTextField labelWithString:item.displayName];
  actionLabel.translatesAutoresizingMaskIntoConstraints = NO;
  actionLabel.font = [NSFont systemFontOfSize:13.0];
  actionLabel.lineBreakMode = NSLineBreakByTruncatingTail;
  [actionLabel setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                        forOrientation:NSLayoutConstraintOrientationHorizontal];

  _enabledSwitch = [[NSSwitch alloc] initWithFrame:NSZeroRect];
  _enabledSwitch.translatesAutoresizingMaskIntoConstraints = NO;
  _enabledSwitch.controlSize = NSControlSizeSmall;
  _enabledSwitch.state = item.enabled ? NSControlStateValueOn : NSControlStateValueOff;
  _enabledSwitch.target = self;
  _enabledSwitch.action = @selector(enabledChanged:);

  _recorder = [[SHShortcutRecorderView alloc] initWithFrame:NSZeroRect];
  [_recorder setShortcutKey:item.key modifiers:SHParseModifiers(item.modifiersText ?: @"")];
  _recorder.activeAppearance = item.enabled;
  _recorder.onRecordingChanged = recordingChanged;

  [self addSubview:actionLabel];
  [self addSubview:_enabledSwitch];
  [self addSubview:_recorder];

  [NSLayoutConstraint activateConstraints:@[
    [actionLabel.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:kRowInsetX],
    [actionLabel.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],

    [_recorder.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-kRowInsetX],
    [_recorder.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
    [_recorder.topAnchor constraintEqualToAnchor:self.topAnchor constant:kRowInsetY],
    [_recorder.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-kRowInsetY],

    [_enabledSwitch.trailingAnchor constraintEqualToAnchor:_recorder.leadingAnchor constant:-14.0],
    [_enabledSwitch.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
    [_enabledSwitch.leadingAnchor constraintGreaterThanOrEqualToAnchor:actionLabel.trailingAnchor constant:12.0],
  ]];

  return self;
}

- (void)enabledChanged:(id)sender {
  (void)sender;
  _recorder.activeAppearance = (_enabledSwitch.state == NSControlStateValueOn);
}

- (SHHotkeyItem *)currentItem {
  SHHotkeyItem *item = [[SHHotkeyItem alloc] init];
  item.actionID = self.item.actionID;
  item.sectionTitle = self.item.sectionTitle;
  item.displayName = self.item.displayName;
  item.enabled = (_enabledSwitch.state == NSControlStateValueOn);
  item.key = _recorder.keyString ?: @"";
  item.modifiersText = [_recorder modifiersText];
  return item;
}

@end

#pragma mark - SHSettingsWindowController

@interface SHSettingsWindowController ()

@property(nonatomic, strong) NSTextField *settingsPathField;
@property(nonatomic, strong) NSSwitch *launchAtLoginButton;
@property(nonatomic, strong) NSSwitch *betaUpdatesButton;
@property(nonatomic, strong) NSView *betaUpdatesRow;
@property(nonatomic, strong) NSSwitch *workspaceWrapButton;
@property(nonatomic, strong) NSSwitch *displayWrapButton;
@property(nonatomic, strong) NSSwitch *trayScrollButton;
@property(nonatomic, strong) NSSwitch *trayScrollInvertedButton;
@property(nonatomic, strong) NSView *trayScrollInvertedRow;
@property(nonatomic, strong) NSSwitch *fastSwipeButton;
@property(nonatomic, strong) NSStackView *hotkeysStackView;
@property(nonatomic, strong) NSTextField *statusLabel;
@property(nonatomic, copy) NSArray<SHHotkeyRowView *> *hotkeyRowViews;

// YES while a recorder is capturing. The runtime block is only engaged when
// this is true *and* the settings window is key, so hotkeys stay live whenever
// the window is in the background.
@property(nonatomic, assign) BOOL recorderListening;

@end

@implementation SHSettingsWindowController

- (instancetype)init {
  NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0.0, 0.0, 620.0, 720.0)
                                                 styleMask:(NSWindowStyleMaskTitled |
                                                            NSWindowStyleMaskClosable |
                                                            NSWindowStyleMaskMiniaturizable |
                                                            NSWindowStyleMaskResizable)
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
  window.title = @"SpaceHound Settings";
  window.releasedWhenClosed = NO;
  window.minSize = NSMakeSize(560.0, 560.0);
  window.frameAutosaveName = @"SpaceHoundSettingsWindow";

  self = [super initWithWindow:window];
  if (self == nil) {
    return nil;
  }

  window.delegate = self;
  [self buildInterface];

  // The content rect is anchored at the screen origin (bottom-left). Center the
  // window on first launch; a previously saved autosave frame, if any, wins.
  if (![window setFrameUsingName:window.frameAutosaveName]) {
    [window center];
  }

  return self;
}

- (void)showWindowAndActivate {
  os_log_info(SHLogSettings(), "Opening settings window");
  [self reloadFromDisk:nil];
  [self showWindow:nil];
  [NSApp activateIgnoringOtherApps:YES];
  [self.window makeKeyAndOrderFront:nil];
}

- (void)buildInterface {
  NSView *contentView = self.window.contentView;
  contentView.wantsLayer = YES;

  NSStackView *rootStack = [[NSStackView alloc] initWithFrame:NSZeroRect];
  rootStack.translatesAutoresizingMaskIntoConstraints = NO;
  rootStack.orientation = NSUserInterfaceLayoutOrientationVertical;
  rootStack.alignment = NSLayoutAttributeLeading;
  rootStack.spacing = 20.0;

  // General section.
  self.launchAtLoginButton = [self makeSwitch];
  self.betaUpdatesButton = [self makeSwitch];
  self.betaUpdatesButton.target = self;
  self.betaUpdatesButton.action = @selector(betaUpdatesChanged:);
  self.workspaceWrapButton = [self makeSwitch];
  self.displayWrapButton = [self makeSwitch];
  self.trayScrollButton = [self makeSwitch];
  self.trayScrollButton.target = self;
  self.trayScrollButton.action = @selector(trayScrollChanged:);
  self.trayScrollInvertedButton = [self makeSwitch];
  self.fastSwipeButton = [self makeSwitch];

  self.trayScrollInvertedRow = [self toggleRowForSwitch:self.trayScrollInvertedButton
                                                  title:@"Invert tray scroll direction"
                                               subtitle:@"Reverse the scroll direction for switching."];
  self.betaUpdatesRow =
      [self toggleRowForSwitch:self.betaUpdatesButton
                         title:@"Receive beta updates"
                      subtitle:@"Get beta releases before production. They're signed and notarized "
                               @"the same way but may contain unfinished changes."];

  NSArray<NSView *> *generalRows = @[
    [self toggleRowForSwitch:self.launchAtLoginButton
                       title:@"Launch at login"
                    subtitle:@"Automatically open SpaceHound when you sign in."],
    self.betaUpdatesRow,
    [self toggleRowForSwitch:self.workspaceWrapButton
                       title:@"Wrap workspace navigation"
                    subtitle:@"Loop back to the first workspace after the last."],
    [self toggleRowForSwitch:self.displayWrapButton
                       title:@"Wrap display navigation"
                    subtitle:@"Loop across the left and right display edges."],
    [self toggleRowForSwitch:self.trayScrollButton
                       title:@"Enable tray scroll switching"
                    subtitle:@"Scroll over the menu bar icon to change workspaces."],
    self.trayScrollInvertedRow,
    [self toggleRowForSwitch:self.fastSwipeButton
                       title:@"Enable fast swipe"
                    subtitle:@"Trigger swipe actions with a lighter, quicker gesture."],
  ];
  SHCardView *generalCard = [self cardWithRows:generalRows];

  // Hotkeys section. The stack lives directly inside the card now; the whole
  // window scrolls rather than the hotkey list scrolling on its own.
  self.hotkeysStackView = [[NSStackView alloc] initWithFrame:NSZeroRect];
  self.hotkeysStackView.translatesAutoresizingMaskIntoConstraints = NO;
  self.hotkeysStackView.orientation = NSUserInterfaceLayoutOrientationVertical;
  self.hotkeysStackView.alignment = NSLayoutAttributeLeading;
  self.hotkeysStackView.spacing = 0.0;

  SHCardView *hotkeysCard = [[SHCardView alloc] initWithFrame:NSZeroRect];
  [hotkeysCard addSubview:self.hotkeysStackView];
  [NSLayoutConstraint activateConstraints:@[
    [self.hotkeysStackView.leadingAnchor constraintEqualToAnchor:hotkeysCard.leadingAnchor constant:1.0],
    [self.hotkeysStackView.trailingAnchor constraintEqualToAnchor:hotkeysCard.trailingAnchor constant:-1.0],
    [self.hotkeysStackView.topAnchor constraintEqualToAnchor:hotkeysCard.topAnchor constant:6.0],
    [self.hotkeysStackView.bottomAnchor constraintEqualToAnchor:hotkeysCard.bottomAnchor constant:-6.0],
  ]];

  // Footer.
  self.settingsPathField = [NSTextField labelWithString:@""];
  self.settingsPathField.translatesAutoresizingMaskIntoConstraints = NO;
  self.settingsPathField.font = [NSFont monospacedSystemFontOfSize:10.0 weight:NSFontWeightRegular];
  self.settingsPathField.textColor = [NSColor tertiaryLabelColor];
  self.settingsPathField.lineBreakMode = NSLineBreakByTruncatingMiddle;
  self.settingsPathField.allowsExpansionToolTips = YES;
  [self.settingsPathField setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow - 1
                                                   forOrientation:NSLayoutConstraintOrientationHorizontal];
  [self.settingsPathField setContentHuggingPriority:NSLayoutPriorityDefaultLow
                                     forOrientation:NSLayoutConstraintOrientationHorizontal];

  self.statusLabel = [NSTextField labelWithString:@""];
  self.statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
  self.statusLabel.font = [NSFont systemFontOfSize:11.0];
  self.statusLabel.textColor = [NSColor secondaryLabelColor];

  NSButton *revealButton = [NSButton buttonWithTitle:@"Reveal in Finder"
                                              target:self
                                              action:@selector(revealSettingsFile:)];
  NSButton *reloadButton = [NSButton buttonWithTitle:@"Reload"
                                              target:self
                                              action:@selector(reloadFromDisk:)];
  NSButton *saveButton = [NSButton buttonWithTitle:@"Save"
                                            target:self
                                            action:@selector(saveSettings:)];
  saveButton.keyEquivalent = @"\r";
  saveButton.bezelStyle = NSBezelStyleRounded;

  NSStackView *buttonRow = [[NSStackView alloc] initWithFrame:NSZeroRect];
  buttonRow.translatesAutoresizingMaskIntoConstraints = NO;
  buttonRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  buttonRow.alignment = NSLayoutAttributeCenterY;
  buttonRow.spacing = 8.0;
  [buttonRow addArrangedSubview:self.settingsPathField];
  [buttonRow addArrangedSubview:self.statusLabel];
  [buttonRow addArrangedSubview:revealButton];
  [buttonRow addArrangedSubview:reloadButton];
  [buttonRow addArrangedSubview:saveButton];

  // Assemble. Hotkeys come first, general settings last; the footer stays
  // pinned while everything above it scrolls as one document.
  NSView *generalHeader = [self groupHeaderTitle:@"General" subtitle:nil];
  NSView *hotkeysHeader = [self groupHeaderTitle:@"Hotkeys"
                                        subtitle:@"Click a shortcut to record a new combination. Turn a row off to disable it."];

  [rootStack addArrangedSubview:hotkeysHeader];
  [rootStack addArrangedSubview:hotkeysCard];
  [rootStack addArrangedSubview:generalHeader];
  [rootStack addArrangedSubview:generalCard];

  for (NSView *view in @[ hotkeysHeader, hotkeysCard, generalHeader, generalCard ]) {
    [view.widthAnchor constraintEqualToAnchor:rootStack.widthAnchor].active = YES;
  }
  [rootStack setCustomSpacing:8.0 afterView:hotkeysHeader];
  [rootStack setCustomSpacing:8.0 afterView:generalHeader];

  // A plain container is the document view so it fills the full viewport width
  // (the clip view pins its document to the origin, so insetting the document
  // itself would just left-align it). The horizontal margin lives on the stack
  // inside the container instead, which insets reliably and keeps the scroller —
  // sitting at the window edge — clear of the cards.
  NSView *documentView = [[NSView alloc] initWithFrame:NSZeroRect];
  documentView.translatesAutoresizingMaskIntoConstraints = NO;
  [documentView addSubview:rootStack];

  NSScrollView *scrollView = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  scrollView.translatesAutoresizingMaskIntoConstraints = NO;
  scrollView.hasVerticalScroller = YES;
  scrollView.borderType = NSNoBorder;
  scrollView.drawsBackground = NO;
  scrollView.documentView = documentView;

  [contentView addSubview:scrollView];
  [contentView addSubview:buttonRow];

  [NSLayoutConstraint activateConstraints:@[
    // Container fills the viewport width; the stack's height drives the scroll
    // length, and the stack sits inset by an equal margin on each side.
    [documentView.topAnchor constraintEqualToAnchor:scrollView.contentView.topAnchor],
    [documentView.leadingAnchor constraintEqualToAnchor:scrollView.contentView.leadingAnchor],
    [documentView.trailingAnchor constraintEqualToAnchor:scrollView.contentView.trailingAnchor],
    [documentView.widthAnchor constraintEqualToAnchor:scrollView.contentView.widthAnchor],

    [rootStack.topAnchor constraintEqualToAnchor:documentView.topAnchor],
    [rootStack.bottomAnchor constraintEqualToAnchor:documentView.bottomAnchor],
    [rootStack.leadingAnchor constraintEqualToAnchor:documentView.leadingAnchor constant:24.0],
    [rootStack.trailingAnchor constraintEqualToAnchor:documentView.trailingAnchor constant:-24.0],

    [scrollView.topAnchor constraintEqualToAnchor:contentView.topAnchor constant:24.0],
    [scrollView.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor],
    [scrollView.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor],

    [buttonRow.topAnchor constraintEqualToAnchor:scrollView.bottomAnchor constant:16.0],
    [buttonRow.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor constant:24.0],
    [buttonRow.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor constant:-24.0],
    [buttonRow.bottomAnchor constraintEqualToAnchor:contentView.bottomAnchor constant:-20.0],
  ]];

  // Keep the window from opening with a shortcut recorder focused.
  self.window.initialFirstResponder = self.workspaceWrapButton;
}

#pragma mark - Building blocks

- (NSSwitch *)makeSwitch {
  NSSwitch *toggle = [[NSSwitch alloc] initWithFrame:NSZeroRect];
  toggle.translatesAutoresizingMaskIntoConstraints = NO;
  return toggle;
}

- (NSView *)groupHeaderTitle:(NSString *)title subtitle:(NSString *)subtitle {
  NSStackView *stack = [[NSStackView alloc] initWithFrame:NSZeroRect];
  stack.translatesAutoresizingMaskIntoConstraints = NO;
  stack.orientation = NSUserInterfaceLayoutOrientationVertical;
  stack.alignment = NSLayoutAttributeLeading;
  stack.spacing = 3.0;

  NSTextField *titleLabel = [NSTextField labelWithString:title];
  titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
  titleLabel.font = [NSFont systemFontOfSize:15.0 weight:NSFontWeightSemibold];
  [stack addArrangedSubview:titleLabel];

  if (subtitle.length > 0) {
    NSTextField *subtitleLabel = [NSTextField labelWithString:subtitle];
    subtitleLabel.translatesAutoresizingMaskIntoConstraints = NO;
    subtitleLabel.font = [NSFont systemFontOfSize:11.0];
    subtitleLabel.textColor = [NSColor secondaryLabelColor];
    subtitleLabel.lineBreakMode = NSLineBreakByWordWrapping;
    subtitleLabel.maximumNumberOfLines = 0;
    [stack addArrangedSubview:subtitleLabel];
    [subtitleLabel.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;
  }

  return stack;
}

- (NSView *)toggleRowForSwitch:(NSSwitch *)toggle title:(NSString *)title subtitle:(NSString *)subtitle {
  NSView *row = [[NSView alloc] initWithFrame:NSZeroRect];
  row.translatesAutoresizingMaskIntoConstraints = NO;

  NSTextField *titleLabel = [NSTextField labelWithString:title];
  titleLabel.translatesAutoresizingMaskIntoConstraints = NO;
  titleLabel.font = [NSFont systemFontOfSize:13.0];
  [titleLabel setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                       forOrientation:NSLayoutConstraintOrientationHorizontal];

  [row addSubview:titleLabel];
  [row addSubview:toggle];

  NSTextField *subtitleLabel = nil;
  if (subtitle.length > 0) {
    subtitleLabel = [NSTextField labelWithString:subtitle];
    subtitleLabel.translatesAutoresizingMaskIntoConstraints = NO;
    subtitleLabel.font = [NSFont systemFontOfSize:11.0];
    subtitleLabel.textColor = [NSColor secondaryLabelColor];
    subtitleLabel.lineBreakMode = NSLineBreakByWordWrapping;
    subtitleLabel.maximumNumberOfLines = 0;
    [subtitleLabel setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                            forOrientation:NSLayoutConstraintOrientationHorizontal];
    [row addSubview:subtitleLabel];
  }

  NSMutableArray<NSLayoutConstraint *> *constraints = [NSMutableArray array];
  [constraints addObjectsFromArray:@[
    [titleLabel.leadingAnchor constraintEqualToAnchor:row.leadingAnchor constant:kRowInsetX],
    [titleLabel.topAnchor constraintEqualToAnchor:row.topAnchor constant:kRowInsetY],
    [toggle.trailingAnchor constraintEqualToAnchor:row.trailingAnchor constant:-kRowInsetX],
    [toggle.centerYAnchor constraintEqualToAnchor:row.centerYAnchor],
    [toggle.leadingAnchor constraintGreaterThanOrEqualToAnchor:titleLabel.trailingAnchor constant:12.0],
  ]];

  if (subtitleLabel != nil) {
    [constraints addObjectsFromArray:@[
      [subtitleLabel.leadingAnchor constraintEqualToAnchor:titleLabel.leadingAnchor],
      [subtitleLabel.topAnchor constraintEqualToAnchor:titleLabel.bottomAnchor constant:2.0],
      [subtitleLabel.bottomAnchor constraintEqualToAnchor:row.bottomAnchor constant:-kRowInsetY],
      [subtitleLabel.trailingAnchor constraintLessThanOrEqualToAnchor:toggle.leadingAnchor constant:-12.0],
    ]];
  } else {
    [constraints addObject:[titleLabel.bottomAnchor constraintEqualToAnchor:row.bottomAnchor constant:-kRowInsetY]];
  }

  [NSLayoutConstraint activateConstraints:constraints];
  return row;
}

- (NSView *)separatorLine {
  NSBox *box = [[NSBox alloc] initWithFrame:NSZeroRect];
  box.boxType = NSBoxSeparator;
  box.translatesAutoresizingMaskIntoConstraints = NO;
  [box.heightAnchor constraintEqualToConstant:1.0].active = YES;
  return box;
}

- (SHCardView *)cardWithRows:(NSArray<NSView *> *)rows {
  SHCardView *card = [[SHCardView alloc] initWithFrame:NSZeroRect];

  NSStackView *stack = [[NSStackView alloc] initWithFrame:NSZeroRect];
  stack.translatesAutoresizingMaskIntoConstraints = NO;
  stack.orientation = NSUserInterfaceLayoutOrientationVertical;
  stack.alignment = NSLayoutAttributeLeading;
  stack.spacing = 0.0;

  NSMutableArray<NSView *> *fullWidthViews = [NSMutableArray array];
  for (NSUInteger index = 0; index < rows.count; index++) {
    if (index > 0) {
      NSView *separator = [self separatorLine];
      [stack addArrangedSubview:separator];
      [fullWidthViews addObject:separator];
    }
    [stack addArrangedSubview:rows[index]];
    [fullWidthViews addObject:rows[index]];
  }

  [card addSubview:stack];
  [NSLayoutConstraint activateConstraints:@[
    [stack.leadingAnchor constraintEqualToAnchor:card.leadingAnchor],
    [stack.trailingAnchor constraintEqualToAnchor:card.trailingAnchor],
    [stack.topAnchor constraintEqualToAnchor:card.topAnchor],
    [stack.bottomAnchor constraintEqualToAnchor:card.bottomAnchor],
  ]];

  for (NSView *view in fullWidthViews) {
    [view.widthAnchor constraintEqualToAnchor:stack.widthAnchor].active = YES;
  }

  return card;
}

- (NSView *)hotkeySectionHeaderWithTitle:(NSString *)title topSeparator:(BOOL)topSeparator {
  NSView *row = [[NSView alloc] initWithFrame:NSZeroRect];
  row.translatesAutoresizingMaskIntoConstraints = NO;

  NSTextField *label = [NSTextField labelWithString:title.uppercaseString];
  label.translatesAutoresizingMaskIntoConstraints = NO;
  label.font = [NSFont systemFontOfSize:11.0 weight:NSFontWeightSemibold];
  label.textColor = [NSColor secondaryLabelColor];
  [row addSubview:label];

  [NSLayoutConstraint activateConstraints:@[
    [label.leadingAnchor constraintEqualToAnchor:row.leadingAnchor constant:kRowInsetX],
    [label.topAnchor constraintEqualToAnchor:row.topAnchor constant:topSeparator ? 14.0 : 12.0],
    [label.bottomAnchor constraintEqualToAnchor:row.bottomAnchor constant:-6.0],
    [label.trailingAnchor constraintLessThanOrEqualToAnchor:row.trailingAnchor constant:-kRowInsetX],
  ]];

  return row;
}

- (void)addFullWidthArrangedView:(NSView *)view {
  [self.hotkeysStackView addArrangedSubview:view];
  [view.widthAnchor constraintEqualToAnchor:self.hotkeysStackView.widthAnchor].active = YES;
}

#pragma mark - Actions

- (void)reloadFromDisk:(id)sender {
  (void)sender;

  os_log_debug(SHLogSettings(), "Reloading settings from disk");
  NSError *pathError = nil;
  NSURL *settingsURL = [SHSettingsStore settingsFileURL:&pathError];
  if (settingsURL == nil) {
    NSError *effectiveError = pathError ?: [NSError errorWithDomain:NSCocoaErrorDomain
                                                               code:NSFileReadUnknownError
                                                           userInfo:@{NSLocalizedDescriptionKey : @"Failed to locate settings.json."}];
    os_log_error(SHLogSettings(),
                 "Failed to locate settings (domain=%{private}@ code=%{public}ld)",
                 effectiveError.domain,
                 (long)effectiveError.code);
    [self presentSettingsError:effectiveError];
    return;
  }

  NSError *loadError = nil;
  SHSettingsDocument *document = [SHSettingsStore loadDocument:&loadError];
  if (document == nil) {
    NSError *effectiveError = loadError ?: [NSError errorWithDomain:NSCocoaErrorDomain
                                                               code:NSFileReadUnknownError
                                                           userInfo:@{NSLocalizedDescriptionKey : @"Failed to load settings.json."}];
    os_log_error(SHLogSettings(),
                 "Failed to load settings (domain=%{private}@ code=%{public}ld)",
                 effectiveError.domain,
                 (long)effectiveError.code);
    [self presentSettingsError:effectiveError];
    return;
  }

  self.settingsPathField.stringValue = settingsURL.path ?: @"";
  [self applyDocumentToControls:document];
  [self reloadLaunchAtLoginState];
  [self reloadBetaUpdatesState];
  self.statusLabel.stringValue = @"";
  os_log_info(SHLogSettings(), "Settings reloaded from disk");
}

- (void)saveSettings:(id)sender {
  (void)sender;

  os_log_info(SHLogSettings(), "Saving settings");
  SHSettingsDocument *document = [[SHSettingsDocument alloc] init];
  document.version = @"1.0";
  document.workspaceWrap = (self.workspaceWrapButton.state == NSControlStateValueOn);
  document.displayWrap = (self.displayWrapButton.state == NSControlStateValueOn);
  document.trayScroll = (self.trayScrollButton.state == NSControlStateValueOn);
  document.trayScrollInverted = (self.trayScrollInvertedButton.state == NSControlStateValueOn);
  document.fastSwipe = (self.fastSwipeButton.state == NSControlStateValueOn);

  NSMutableArray<SHHotkeyItem *> *hotkeys = [NSMutableArray arrayWithCapacity:self.hotkeyRowViews.count];
  for (SHHotkeyRowView *rowView in self.hotkeyRowViews) {
    [hotkeys addObject:[rowView currentItem]];
  }
  document.hotkeys = [hotkeys copy];

  NSError *saveError = nil;
  if (![SHSettingsStore saveDocument:document error:&saveError]) {
    NSError *effectiveError = saveError ?: [NSError errorWithDomain:NSCocoaErrorDomain
                                                               code:NSFileWriteUnknownError
                                                           userInfo:@{NSLocalizedDescriptionKey : @"Failed to save settings.json."}];
    os_log_error(SHLogSettings(),
                 "Failed to save settings (domain=%{private}@ code=%{public}ld)",
                 effectiveError.domain,
                 (long)effectiveError.code);
    [self presentSettingsError:effectiveError];
    return;
  }

  if (self.applyHandler != nil) {
    NSError *applyError = nil;
    if (!self.applyHandler(&applyError)) {
      os_log_error(SHLogSettings(),
                   "Runtime failed to apply saved settings (domain=%{private}@ code=%{public}ld)",
                   applyError.domain,
                   (long)applyError.code);
      self.statusLabel.stringValue = @"Saved to disk, but the runtime could not apply the update.";
      if (applyError != nil) {
        [self presentSettingsError:applyError];
      }
      return;
    }
  }

  // Written before the login-item step, which can return early for approval.
  const BOOL wantsBetaUpdates = (self.betaUpdatesButton.state == NSControlStateValueOn);
  if (self.updateChannelSelectable && wantsBetaUpdates != [SHUpdateChannel receivesBetaUpdates]) {
    [SHUpdateChannel setReceivesBetaUpdates:wantsBetaUpdates];
    if (wantsBetaUpdates) {
      os_log_info(SHLogUpdates(), "Beta update channel enabled");
    } else {
      os_log_info(SHLogUpdates(), "Beta update channel disabled");
    }
    if (self.updateChannelChangedHandler != nil) {
      self.updateChannelChangedHandler();
    }
  }

  const BOOL launchAtLoginEnabled =
      (self.launchAtLoginButton.state == NSControlStateValueOn);
  NSError *loginItemError = nil;
  const BOOL loginItemChanged =
      [SHLoginItemManager setEnabled:launchAtLoginEnabled error:&loginItemError];
  const SHLoginItemStatus loginItemStatus = [SHLoginItemManager status];
  [self reloadLaunchAtLoginState];

  if (launchAtLoginEnabled && loginItemStatus == SHLoginItemStatusRequiresApproval) {
    os_log_info(SHLogLoginItem(), "Launch at login requires approval");
    self.statusLabel.stringValue = @"Saved. Launch at login requires approval.";
    [self presentLaunchAtLoginApproval];
    return;
  }

  const BOOL loginItemMatchesRequestedState = launchAtLoginEnabled
      ? loginItemStatus == SHLoginItemStatusEnabled
      : (loginItemStatus == SHLoginItemStatusNotRegistered ||
         loginItemStatus == SHLoginItemStatusNotFound);
  if (!loginItemChanged || !loginItemMatchesRequestedState) {
    os_log_error(SHLogLoginItem(),
                 "Launch at login did not reach the requested state (domain=%{private}@ code=%{public}ld)",
                 loginItemError.domain,
                 (long)loginItemError.code);
    self.statusLabel.stringValue = @"Settings saved, but launch at login could not be updated.";
    [self presentSettingsError:loginItemError ?:
        [NSError errorWithDomain:@"com.jjkr.spacehound.LoginItem"
                            code:1
                        userInfo:@{
                          NSLocalizedDescriptionKey :
                              @"Launch at login could not be updated."
                        }]];
    return;
  }

  self.statusLabel.stringValue = @"Saved and applied.";
  os_log_info(SHLogSettings(), "Settings saved and applied");
}

- (void)reloadLaunchAtLoginState {
  const SHLoginItemStatus status = [SHLoginItemManager status];
  const BOOL isRegistered =
      status == SHLoginItemStatusEnabled || status == SHLoginItemStatusRequiresApproval;
  self.launchAtLoginButton.state = isRegistered ? NSControlStateValueOn : NSControlStateValueOff;
}

- (void)reloadBetaUpdatesState {
  const BOOL selectable = self.updateChannelSelectable;
  self.betaUpdatesButton.state =
      [SHUpdateChannel receivesBetaUpdates] ? NSControlStateValueOn : NSControlStateValueOff;
  self.betaUpdatesButton.enabled = selectable;
  self.betaUpdatesRow.alphaValue = selectable ? 1.0 : 0.45;
}

// Confirms the opt-in when the switch is flipped on; the preference itself is
// only written on Save.
- (void)betaUpdatesChanged:(id)sender {
  (void)sender;
  if (self.betaUpdatesButton.state != NSControlStateValueOn) {
    return;
  }

  NSAlert *alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleWarning;
  alert.messageText = @"Receive beta updates?";
  alert.informativeText =
      @"Beta updates are signed and notarized like production releases, but may contain "
       "unfinished changes. You can return to production updates from Settings at any time.";
  [alert addButtonWithTitle:@"Receive Beta Updates"];
  [alert addButtonWithTitle:@"Cancel"];
  __weak SHSettingsWindowController *weakSelf = self;
  [alert beginSheetModalForWindow:self.window
                completionHandler:^(NSModalResponse returnCode) {
                  if (returnCode != NSAlertFirstButtonReturn) {
                    os_log_info(SHLogUpdates(), "Beta update opt-in cancelled");
                    weakSelf.betaUpdatesButton.state = NSControlStateValueOff;
                  }
                }];
}

- (void)presentLaunchAtLoginApproval {
  NSAlert *alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleInformational;
  alert.messageText = @"Approve SpaceHound in Login Items";
  alert.informativeText =
      @"macOS requires your approval before SpaceHound can launch automatically. "
       "Open Login Items and enable SpaceHound.";
  [alert addButtonWithTitle:@"Open Login Items"];
  [alert addButtonWithTitle:@"Not Now"];
  [alert beginSheetModalForWindow:self.window
                completionHandler:^(NSModalResponse returnCode) {
                  if (returnCode == NSAlertFirstButtonReturn) {
                    [SHLoginItemManager openSystemSettings];
                  }
                }];
}

- (void)revealSettingsFile:(id)sender {
  (void)sender;

  os_log_info(SHLogSettings(), "Reveal settings file requested");
  NSError *error = nil;
  NSURL *settingsURL = [SHSettingsStore settingsFileURL:&error];
  if (settingsURL == nil) {
    NSError *effectiveError = error ?: [NSError errorWithDomain:NSCocoaErrorDomain
                                                           code:NSFileNoSuchFileError
                                                       userInfo:@{NSLocalizedDescriptionKey : @"Failed to locate settings.json."}];
    os_log_error(SHLogSettings(),
                 "Failed to reveal settings (domain=%{private}@ code=%{public}ld)",
                 effectiveError.domain,
                 (long)effectiveError.code);
    [self presentSettingsError:effectiveError];
    return;
  }

  [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[settingsURL]];
  os_log_info(SHLogSettings(), "Settings file revealed in Finder");
}

- (void)trayScrollChanged:(id)sender {
  (void)sender;
  BOOL trayScrollEnabled = (self.trayScrollButton.state == NSControlStateValueOn);
  self.trayScrollInvertedButton.enabled = trayScrollEnabled;
  self.trayScrollInvertedRow.alphaValue = trayScrollEnabled ? 1.0 : 0.45;
  if (!trayScrollEnabled) {
    self.trayScrollInvertedButton.state = NSControlStateValueOff;
  }
}

- (void)applyDocumentToControls:(SHSettingsDocument *)document {
  self.workspaceWrapButton.state = document.workspaceWrap ? NSControlStateValueOn : NSControlStateValueOff;
  self.displayWrapButton.state = document.displayWrap ? NSControlStateValueOn : NSControlStateValueOff;
  self.trayScrollButton.state = document.trayScroll ? NSControlStateValueOn : NSControlStateValueOff;
  self.trayScrollInvertedButton.state =
      document.trayScrollInverted ? NSControlStateValueOn : NSControlStateValueOff;
  self.fastSwipeButton.state = document.fastSwipe ? NSControlStateValueOn : NSControlStateValueOff;
  [self trayScrollChanged:nil];

  for (NSView *view in [self.hotkeysStackView.arrangedSubviews copy]) {
    [self.hotkeysStackView removeArrangedSubview:view];
    [view removeFromSuperview];
  }

  __weak SHSettingsWindowController *weakSelf = self;
  void (^recordingChanged)(BOOL) = ^(BOOL recording) {
    SHSettingsWindowController *strongSelf = weakSelf;
    if (strongSelf == nil) {
      return;
    }
    strongSelf.recorderListening = recording;
    [strongSelf updateInputSuspension];
  };

  NSMutableArray<SHHotkeyRowView *> *rowViews = [NSMutableArray arrayWithCapacity:document.hotkeys.count];
  NSString *currentSection = nil;
  BOOL isFirstRow = YES;
  for (SHHotkeyItem *item in document.hotkeys) {
    if (![currentSection isEqualToString:item.sectionTitle]) {
      currentSection = item.sectionTitle;
      [self addFullWidthArrangedView:[self hotkeySectionHeaderWithTitle:currentSection
                                                           topSeparator:!isFirstRow]];
    } else {
      [self addFullWidthArrangedView:[self separatorLine]];
    }

    SHHotkeyRowView *rowView = [[SHHotkeyRowView alloc] initWithHotkeyItem:item
                                                         recordingChanged:recordingChanged];
    [rowViews addObject:rowView];
    [self addFullWidthArrangedView:rowView];
    isFirstRow = NO;
  }

  self.hotkeyRowViews = [rowViews copy];
}

#pragma mark - NSWindowDelegate

// Engage the runtime hotkey block only while a recorder is listening *and* the
// settings window is key. Losing focus reactivates hotkeys; regaining focus
// with a recorder still armed re-suspends them so it keeps listening.
- (void)updateInputSuspension {
  if (self.inputSuspensionHandler == nil) {
    return;
  }
  BOOL shouldSuspend = self.recorderListening && self.window.isKeyWindow;
  self.inputSuspensionHandler(shouldSuspend);
}

- (void)windowDidBecomeKey:(NSNotification *)notification {
  (void)notification;
  [self updateInputSuspension];
}

- (void)windowDidResignKey:(NSNotification *)notification {
  (void)notification;
  [self updateInputSuspension];
}

- (void)windowWillClose:(NSNotification *)notification {
  (void)notification;
  // Closing must never leave global hotkey handling suspended. Resigning first
  // responder on close isn't guaranteed to fire the recorder's own reset.
  self.recorderListening = NO;
  [self updateInputSuspension];
}

- (void)presentSettingsError:(NSError *)error {
  NSAlert *alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleWarning;
  alert.messageText = @"SpaceHound Settings";
  alert.informativeText = error.localizedDescription ?: @"An unknown error occurred.";
  [alert beginSheetModalForWindow:self.window completionHandler:nil];
}

@end
