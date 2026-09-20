// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#import "SHSettingsWindowController.h"

#import "SHCrashReporting.h"
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

// Keys of the unsaved-changes snapshot, one per backing store.
static NSString *const kSnapshotDocumentKey = @"document";
static NSString *const kSnapshotBetaUpdatesKey = @"betaUpdates";
static NSString *const kSnapshotCrashReportingKey = @"crashReporting";
static NSString *const kSnapshotLaunchAtLoginKey = @"launchAtLogin";

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
// Fires whenever the enabled switch or the recorded shortcut changes.
@property(nonatomic, copy) void (^onChange)(void);

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
  __weak SHHotkeyRowView *weakSelf = self;
  _recorder.onChange = ^{
    [weakSelf notifyChanged];
  };

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
  [self notifyChanged];
}

- (void)notifyChanged {
  if (self.onChange != nil) {
    self.onChange();
  }
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
@property(nonatomic, strong) NSSwitch *crashReportingButton;
@property(nonatomic, strong) NSView *crashReportingRow;
@property(nonatomic, strong) NSSwitch *workspaceWrapButton;
@property(nonatomic, strong) NSSwitch *workspaceTargetsFocusedDisplayButton;
@property(nonatomic, strong) NSSwitch *displayWrapButton;
@property(nonatomic, strong) NSSwitch *moveCursorToTargetDisplayButton;
@property(nonatomic, strong) NSSwitch *trayScrollButton;
@property(nonatomic, strong) NSSwitch *trayScrollInvertedButton;
@property(nonatomic, strong) NSView *trayScrollInvertedRow;
@property(nonatomic, strong) NSSwitch *fastSwipeButton;
@property(nonatomic, strong) NSStackView *hotkeysStackView;
@property(nonatomic, strong) NSTextField *statusLabel;
@property(nonatomic, strong) NSButton *applyButton;
@property(nonatomic, copy) NSArray<SHHotkeyRowView *> *hotkeyRowViews;
// Result of the last save or reload. Shown in the footer whenever there are no
// outstanding edits; the unsaved indicator takes the slot otherwise.
@property(nonatomic, copy) NSString *statusMessage;
// Control state as last loaded from or written to disk, keyed by store (see
// currentSnapshot). nil while the window is closed, which also means "clean".
@property(nonatomic, strong, nullable) NSMutableDictionary<NSString *, id> *baselineSnapshot;
@property(nonatomic, assign) BOOL suddenTerminationDisabled;
@property(nonatomic, assign) BOOL discardPromptPending;

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
  [self loadFromDisk];
  // Regular policy gives the window a real menu bar and Dock presence while it
  // is open; windowWillClose: drops back to a menu-bar-only agent.
  [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

  // The window is reused across open/close cycles and keeps its Space
  // assignment while closed, so a plain managed window would reopen on the
  // Space it was last shown on and drag the user there. MoveToActiveSpace
  // fixes that, but it can't stay set: the window server leaves such windows
  // out of a Space's front-app bookkeeping, so switching away and back would
  // reactivate the app underneath and bury this window. Apply it only when
  // the window actually needs moving and revert once it has arrived (see
  // settleCollectionBehavior). The move happens while the Dock processes the
  // activation, so reverting any earlier, even on the next run-loop turn or
  // in windowDidBecomeKey:, cancels it.
  NSWindow *window = self.window;
  if (!(window.isVisible && window.isOnActiveSpace)) {
    window.collectionBehavior = NSWindowCollectionBehaviorMoveToActiveSpace;
  }
  [self showWindow:nil];
  [NSApp activateIgnoringOtherApps:YES];
}

// Once the window is on screen on the active Space, make it an ordinary
// managed window again. Called from every delegate hook that can follow the
// Space move: windowDidChangeOcclusionState: fires when the window becomes
// visible on the new Space, which is the one that lands after a reopen from a
// different Space.
- (void)settleCollectionBehavior {
  NSWindow *window = self.window;
  if (window.isVisible && window.isOnActiveSpace) {
    window.collectionBehavior = NSWindowCollectionBehaviorManaged;
  }
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
  self.crashReportingButton = [self makeSwitch];
  self.workspaceWrapButton = [self makeSwitch];
  self.workspaceTargetsFocusedDisplayButton = [self makeSwitch];
  self.displayWrapButton = [self makeSwitch];
  self.moveCursorToTargetDisplayButton = [self makeSwitch];
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
  self.crashReportingRow =
      [self toggleRowForSwitch:self.crashReportingButton
                         title:@"Send crash reports"
                      subtitle:@"Send a report to Sentry when SpaceHound crashes. Reports include the "
                               @"stack trace, app version, and macOS version, never your settings, "
                               @"window titles, or identity."];

  NSArray<NSView *> *generalRows = @[
    [self toggleRowForSwitch:self.launchAtLoginButton
                       title:@"Launch at login"
                    subtitle:@"Automatically open SpaceHound when you sign in."],
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

  // Advanced section: the less common options, kept out of the way at the end.
  NSArray<NSView *> *advancedRows = @[
    self.betaUpdatesRow,
    self.crashReportingRow,
    [self toggleRowForSwitch:self.workspaceTargetsFocusedDisplayButton
                       title:@"Switch workspaces on the focused display"
                    subtitle:@"Change the Space on the display with the focused window. When off, the "
                             @"Space changes on the display under the cursor."],
    [self toggleRowForSwitch:self.moveCursorToTargetDisplayButton
                       title:@"Move cursor to the target display"
                    subtitle:@"Jump the cursor to the destination display when switching displays. "
                             @"When off, the cursor stays where it is."],
  ];
  SHCardView *advancedCard = [self cardWithRows:advancedRows];

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

  // Settings file section: the on-disk path with Reveal / Reload alongside it.
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

  NSButton *revealButton = [NSButton buttonWithTitle:@"Reveal in Finder"
                                              target:self
                                              action:@selector(revealSettingsFile:)];
  NSButton *reloadButton = [NSButton buttonWithTitle:@"Reload"
                                              target:self
                                              action:@selector(reloadFromDisk:)];
  SHCardView *settingsFileCard =
      [self cardWithRows:@[ [self settingsFileRowWithButtons:@[ revealButton, reloadButton ]] ]];

  // Footer: status text / unsaved indicator on the left, Cancel / Apply / OK
  // on the right.
  self.statusLabel = [NSTextField labelWithString:@""];
  self.statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
  self.statusLabel.font = [NSFont systemFontOfSize:11.0];
  self.statusLabel.textColor = [NSColor secondaryLabelColor];
  self.statusLabel.lineBreakMode = NSLineBreakByTruncatingTail;
  [self.statusLabel setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow - 1
                                             forOrientation:NSLayoutConstraintOrientationHorizontal];
  [self.statusLabel setContentHuggingPriority:NSLayoutPriorityDefaultLow
                               forOrientation:NSLayoutConstraintOrientationHorizontal];

  NSButton *cancelButton = [NSButton buttonWithTitle:@"Cancel"
                                              target:self
                                              action:@selector(cancel:)];
  cancelButton.keyEquivalent = @"\e";
  cancelButton.bezelStyle = NSBezelStyleRounded;
  self.applyButton = [NSButton buttonWithTitle:@"Apply"
                                        target:self
                                        action:@selector(apply:)];
  self.applyButton.bezelStyle = NSBezelStyleRounded;
  NSButton *okButton = [NSButton buttonWithTitle:@"OK"
                                          target:self
                                          action:@selector(ok:)];
  okButton.keyEquivalent = @"\r";
  okButton.bezelStyle = NSBezelStyleRounded;

  // Gravity areas keep the label pinned left and the buttons pinned right no
  // matter how wide the label is (an empty label would otherwise leave the
  // buttons clumped at the leading edge).
  NSStackView *buttonRow = [[NSStackView alloc] initWithFrame:NSZeroRect];
  buttonRow.translatesAutoresizingMaskIntoConstraints = NO;
  buttonRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  buttonRow.alignment = NSLayoutAttributeCenterY;
  buttonRow.spacing = 8.0;
  [buttonRow addView:self.statusLabel inGravity:NSStackViewGravityLeading];
  [buttonRow addView:cancelButton inGravity:NSStackViewGravityTrailing];
  [buttonRow addView:self.applyButton inGravity:NSStackViewGravityTrailing];
  [buttonRow addView:okButton inGravity:NSStackViewGravityTrailing];

  // Assemble. General first, then hotkeys, the settings file, and finally the
  // advanced options; the footer stays pinned while everything above it scrolls
  // as one document.
  NSView *generalHeader = [self groupHeaderTitle:@"General" subtitle:nil];
  NSView *hotkeysHeader = [self groupHeaderTitle:@"Hotkeys"
                                        subtitle:@"Click a shortcut to record a new combination. Turn a row off to disable it."];
  NSView *settingsFileHeader =
      [self groupHeaderTitle:@"Settings File"
                    subtitle:@"Settings are written here when you press OK or Apply. Reload discards "
                             @"unsaved changes and re-reads the file."];
  NSView *advancedHeader =
      [self groupHeaderTitle:@"Advanced"
                    subtitle:@"Options most people never need to change."];

  NSArray<NSView *> *sections = @[
    generalHeader, generalCard,
    hotkeysHeader, hotkeysCard,
    settingsFileHeader, settingsFileCard,
    advancedHeader, advancedCard,
  ];
  for (NSView *view in sections) {
    [rootStack addArrangedSubview:view];
    [view.widthAnchor constraintEqualToAnchor:rootStack.widthAnchor].active = YES;
  }
  [rootStack setCustomSpacing:8.0 afterView:generalHeader];
  [rootStack setCustomSpacing:8.0 afterView:hotkeysHeader];
  [rootStack setCustomSpacing:8.0 afterView:settingsFileHeader];
  [rootStack setCustomSpacing:8.0 afterView:advancedHeader];

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

// Switches report edits to the unsaved-changes tracker by default. Switches
// that need their own action call settingChanged: themselves.
- (NSSwitch *)makeSwitch {
  NSSwitch *toggle = [[NSSwitch alloc] initWithFrame:NSZeroRect];
  toggle.translatesAutoresizingMaskIntoConstraints = NO;
  toggle.target = self;
  toggle.action = @selector(settingChanged:);
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

// Path label on the left, action buttons on the right, laid out like a toggle
// row so the card matches the General section.
- (NSView *)settingsFileRowWithButtons:(NSArray<NSButton *> *)buttons {
  NSView *row = [[NSView alloc] initWithFrame:NSZeroRect];
  row.translatesAutoresizingMaskIntoConstraints = NO;

  NSStackView *buttonStack = [[NSStackView alloc] initWithFrame:NSZeroRect];
  buttonStack.translatesAutoresizingMaskIntoConstraints = NO;
  buttonStack.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  buttonStack.alignment = NSLayoutAttributeCenterY;
  buttonStack.spacing = 8.0;
  for (NSButton *button in buttons) {
    [buttonStack addArrangedSubview:button];
  }

  [row addSubview:self.settingsPathField];
  [row addSubview:buttonStack];

  [NSLayoutConstraint activateConstraints:@[
    [self.settingsPathField.leadingAnchor constraintEqualToAnchor:row.leadingAnchor constant:kRowInsetX],
    [self.settingsPathField.centerYAnchor constraintEqualToAnchor:row.centerYAnchor],

    [buttonStack.trailingAnchor constraintEqualToAnchor:row.trailingAnchor constant:-kRowInsetX],
    [buttonStack.centerYAnchor constraintEqualToAnchor:row.centerYAnchor],
    [buttonStack.topAnchor constraintEqualToAnchor:row.topAnchor constant:kRowInsetY],
    [buttonStack.bottomAnchor constraintEqualToAnchor:row.bottomAnchor constant:-kRowInsetY],
    [buttonStack.leadingAnchor constraintEqualToAnchor:self.settingsPathField.trailingAnchor constant:12.0],
  ]];

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
  if (!self.hasUnsavedChanges) {
    [self loadFromDisk];
    return;
  }

  NSAlert *alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleWarning;
  alert.messageText = @"Discard unsaved changes?";
  alert.informativeText = @"Reloading re-reads settings.json and throws away the edits you haven't saved.";
  [alert addButtonWithTitle:@"Discard Changes"];
  [alert addButtonWithTitle:@"Cancel"];
  __weak SHSettingsWindowController *weakSelf = self;
  [alert beginSheetModalForWindow:self.window
                completionHandler:^(NSModalResponse returnCode) {
                  if (returnCode == NSAlertFirstButtonReturn) {
                    os_log_info(SHLogSettings(), "Unsaved changes discarded for reload");
                    [weakSelf loadFromDisk];
                  } else {
                    os_log_info(SHLogSettings(), "Reload cancelled to keep unsaved changes");
                  }
                }];
}

// Re-reads everything from disk and resets the unsaved-changes baseline.
- (void)loadFromDisk {
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
  [self reloadCrashReportingState];
  self.statusMessage = @"";
  [self captureBaseline];
  os_log_info(SHLogSettings(), "Settings reloaded from disk");
}

- (void)cancel:(id)sender {
  // Routes through windowShouldClose: so unsaved edits get a prompt, and
  // windowWillClose: so hotkey suspension is always released.
  [self.window performClose:sender];
}

- (void)apply:(id)sender {
  (void)sender;
  [self performSave];
}

- (void)ok:(id)sender {
  // Only a fully successful save closes the window; any error or approval
  // sheet keeps it open so the user can see what happened.
  if ([self performSave]) {
    [self.window performClose:sender];
  }
}

- (BOOL)performSave {
  os_log_info(SHLogSettings(), "Saving settings");
  NSDictionary<NSString *, id> *snapshot = [self currentSnapshot];
  SHSettingsDocument *document = [[SHSettingsDocument alloc] init];
  document.version = @"1.0";
  document.workspaceWrap = (self.workspaceWrapButton.state == NSControlStateValueOn);
  document.workspaceTargetsFocusedDisplay = (self.workspaceTargetsFocusedDisplayButton.state == NSControlStateValueOn);
  document.displayWrap = (self.displayWrapButton.state == NSControlStateValueOn);
  document.moveCursorToTargetDisplay = (self.moveCursorToTargetDisplayButton.state == NSControlStateValueOn);
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
    [self refreshUnsavedState];
    return NO;
  }
  self.baselineSnapshot[kSnapshotDocumentKey] = snapshot[kSnapshotDocumentKey];

  if (self.applyHandler != nil) {
    NSError *applyError = nil;
    if (!self.applyHandler(&applyError)) {
      os_log_error(SHLogSettings(),
                   "Runtime failed to apply saved settings (domain=%{private}@ code=%{public}ld)",
                   applyError.domain,
                   (long)applyError.code);
      self.statusMessage = @"Saved to disk, but the runtime could not apply the update.";
      if (applyError != nil) {
        [self presentSettingsError:applyError];
      }
      [self refreshUnsavedState];
      return NO;
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
  self.baselineSnapshot[kSnapshotBetaUpdatesKey] = snapshot[kSnapshotBetaUpdatesKey];

  // Saving records a choice even when the switch is left off, so the launch
  // prompt never reappears once the user has seen this setting.
  const BOOL wantsCrashReports = (self.crashReportingButton.state == NSControlStateValueOn);
  if (self.crashReportingAvailable &&
      (![SHCrashReporting hasRecordedChoice] || wantsCrashReports != [SHCrashReporting isEnabled])) {
    [SHCrashReporting setEnabled:wantsCrashReports];
    if (wantsCrashReports) {
      os_log_info(SHLogCrashReporting(), "Crash reporting enabled from Settings");
    } else {
      os_log_info(SHLogCrashReporting(), "Crash reporting disabled from Settings");
    }
    if (self.crashReportingChangedHandler != nil) {
      self.crashReportingChangedHandler();
    }
  }
  self.baselineSnapshot[kSnapshotCrashReportingKey] = snapshot[kSnapshotCrashReportingKey];

  const BOOL launchAtLoginEnabled =
      (self.launchAtLoginButton.state == NSControlStateValueOn);
  NSError *loginItemError = nil;
  const BOOL loginItemChanged =
      [SHLoginItemManager setEnabled:launchAtLoginEnabled error:&loginItemError];
  const SHLoginItemStatus loginItemStatus = [SHLoginItemManager status];
  [self reloadLaunchAtLoginState];
  // The switch now reflects whatever the system actually registered.
  self.baselineSnapshot[kSnapshotLaunchAtLoginKey] = [self currentSnapshot][kSnapshotLaunchAtLoginKey];

  if (launchAtLoginEnabled && loginItemStatus == SHLoginItemStatusRequiresApproval) {
    os_log_info(SHLogLoginItem(), "Launch at login requires approval");
    self.statusMessage = @"Saved. Launch at login requires approval.";
    [self refreshUnsavedState];
    [self presentLaunchAtLoginApproval];
    return NO;
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
    self.statusMessage = @"Settings saved, but launch at login could not be updated.";
    [self refreshUnsavedState];
    [self presentSettingsError:loginItemError ?:
        [NSError errorWithDomain:@"com.jjkr.spacehound.LoginItem"
                            code:1
                        userInfo:@{
                          NSLocalizedDescriptionKey :
                              @"Launch at login could not be updated."
                        }]];
    return NO;
  }

  self.statusMessage = @"Saved and applied.";
  [self refreshUnsavedState];
  os_log_info(SHLogSettings(), "Settings saved and applied");
  return YES;
}

#pragma mark - Unsaved changes

// A plain, isEqual:-comparable picture of every control, grouped by the store
// each part is written to so a partially failed save can update the baseline
// for just the parts that landed.
- (NSDictionary<NSString *, id> *)currentSnapshot {
  NSMutableArray *hotkeys = [NSMutableArray arrayWithCapacity:self.hotkeyRowViews.count];
  for (SHHotkeyRowView *rowView in self.hotkeyRowViews) {
    SHHotkeyItem *item = [rowView currentItem];
    [hotkeys addObject:@[ item.actionID, item.key, item.modifiersText, @(item.enabled) ]];
  }
  NSArray *document = @[
    @(self.workspaceWrapButton.state == NSControlStateValueOn),
    @(self.workspaceTargetsFocusedDisplayButton.state == NSControlStateValueOn),
    @(self.displayWrapButton.state == NSControlStateValueOn),
    @(self.moveCursorToTargetDisplayButton.state == NSControlStateValueOn),
    @(self.trayScrollButton.state == NSControlStateValueOn),
    @(self.trayScrollInvertedButton.state == NSControlStateValueOn),
    @(self.fastSwipeButton.state == NSControlStateValueOn),
    hotkeys,
  ];
  return @{
    kSnapshotDocumentKey : document,
    kSnapshotBetaUpdatesKey : @(self.betaUpdatesButton.state == NSControlStateValueOn),
    kSnapshotCrashReportingKey : @(self.crashReportingButton.state == NSControlStateValueOn),
    kSnapshotLaunchAtLoginKey : @(self.launchAtLoginButton.state == NSControlStateValueOn),
  };
}

- (void)captureBaseline {
  self.baselineSnapshot = [[self currentSnapshot] mutableCopy];
  [self refreshUnsavedState];
}

- (BOOL)hasUnsavedChanges {
  return self.baselineSnapshot != nil && ![[self currentSnapshot] isEqual:self.baselineSnapshot];
}

- (void)settingChanged:(id)sender {
  (void)sender;
  [self refreshUnsavedState];
}

// Reflects the dirty state in the footer label and the close widget, and keeps
// sudden termination off while edits are outstanding so a logout can't drop
// them without the prompt.
- (void)refreshUnsavedState {
  const BOOL dirty = self.hasUnsavedChanges;
  self.statusLabel.stringValue = dirty ? @"● Unsaved changes" : (self.statusMessage ?: @"");
  self.window.documentEdited = dirty;
  self.applyButton.enabled = dirty;

  if (dirty != self.suddenTerminationDisabled) {
    if (dirty) {
      [[NSProcessInfo processInfo] disableSuddenTermination];
    } else {
      [[NSProcessInfo processInfo] enableSuddenTermination];
    }
    self.suddenTerminationDisabled = dirty;
  }
}

- (void)confirmDiscardingUnsavedChanges:(void (^)(BOOL proceed))completion {
  if (!self.hasUnsavedChanges) {
    completion(YES);
    return;
  }
  if (self.discardPromptPending) {
    // A prompt is already up; the answer to that one settles this request too.
    completion(NO);
    return;
  }
  self.discardPromptPending = YES;

  NSAlert *alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleWarning;
  alert.messageText = @"Do you want to save the changes made in SpaceHound Settings?";
  alert.informativeText = @"Your changes will be lost if you don't save them.";
  [alert addButtonWithTitle:@"Save"];
  [alert addButtonWithTitle:@"Cancel"];
  NSButton *dontSaveButton = [alert addButtonWithTitle:@"Don't Save"];
  dontSaveButton.keyEquivalent = @"d";
  dontSaveButton.keyEquivalentModifierMask = NSEventModifierFlagCommand;

  __weak SHSettingsWindowController *weakSelf = self;
  [alert beginSheetModalForWindow:self.window
                completionHandler:^(NSModalResponse returnCode) {
                  SHSettingsWindowController *strongSelf = weakSelf;
                  strongSelf.discardPromptPending = NO;
                  if (returnCode == NSAlertFirstButtonReturn) {
                    os_log_info(SHLogSettings(), "Unsaved changes: save chosen");
                    completion(strongSelf != nil && [strongSelf performSave]);
                  } else if (returnCode == NSAlertThirdButtonReturn) {
                    os_log_info(SHLogSettings(), "Unsaved changes: discard chosen");
                    completion(YES);
                  } else {
                    os_log_info(SHLogSettings(), "Unsaved changes: cancel chosen");
                    completion(NO);
                  }
                }];
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

- (void)reloadCrashReportingState {
  const BOOL available = self.crashReportingAvailable;
  self.crashReportingButton.state =
      [SHCrashReporting isEnabled] ? NSControlStateValueOn : NSControlStateValueOff;
  self.crashReportingButton.enabled = available;
  self.crashReportingRow.alphaValue = available ? 1.0 : 0.45;
}

// Confirms the opt-in when the switch is flipped on; the preference itself is
// only written on Save.
- (void)betaUpdatesChanged:(id)sender {
  [self settingChanged:sender];
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
                  [weakSelf refreshUnsavedState];
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
  [self settingChanged:sender];
}

- (void)applyDocumentToControls:(SHSettingsDocument *)document {
  self.workspaceWrapButton.state = document.workspaceWrap ? NSControlStateValueOn : NSControlStateValueOff;
  self.workspaceTargetsFocusedDisplayButton.state =
      document.workspaceTargetsFocusedDisplay ? NSControlStateValueOn : NSControlStateValueOff;
  self.displayWrapButton.state = document.displayWrap ? NSControlStateValueOn : NSControlStateValueOff;
  self.moveCursorToTargetDisplayButton.state =
      document.moveCursorToTargetDisplay ? NSControlStateValueOn : NSControlStateValueOff;
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
    rowView.onChange = ^{
      [weakSelf settingChanged:nil];
    };
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
  [self settleCollectionBehavior];
  [self updateInputSuspension];
}

- (void)windowDidChangeOcclusionState:(NSNotification *)notification {
  (void)notification;
  [self settleCollectionBehavior];
}

- (void)windowDidResignKey:(NSNotification *)notification {
  (void)notification;
  [self updateInputSuspension];
}

// Every close path (footer Cancel, title-bar widget, Window > Close) goes
// through performClose:, so this is the single place unsaved edits get a say.
- (BOOL)windowShouldClose:(NSWindow *)sender {
  if (!self.hasUnsavedChanges) {
    return YES;
  }
  __weak SHSettingsWindowController *weakSelf = self;
  [self confirmDiscardingUnsavedChanges:^(BOOL proceed) {
    if (proceed) {
      [weakSelf.window close];
    }
  }];
  return NO;
}

- (void)windowWillClose:(NSNotification *)notification {
  (void)notification;
  // Closing must never leave global hotkey handling suspended. Resigning first
  // responder on close isn't guaranteed to fire the recorder's own reset.
  self.recorderListening = NO;
  [self updateInputSuspension];

  // Whatever was left unsaved is gone; the next open reloads from disk. Dropping
  // the baseline also re-enables sudden termination.
  self.baselineSnapshot = nil;
  [self refreshUnsavedState];

  // Return to a menu-bar-only agent. Dropping to Accessory while active hands
  // activation to the next app on the current Space. Do not hide the app to
  // do that: once it has been hidden, macOS stops restoring it as the front
  // app when the user returns to the Space the settings window is on, so a
  // reopened window ends up behind other windows after a Space round trip.
  [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

- (void)presentSettingsError:(NSError *)error {
  NSAlert *alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleWarning;
  alert.messageText = @"SpaceHound Settings";
  alert.informativeText = error.localizedDescription ?: @"An unknown error occurred.";
  [alert beginSheetModalForWindow:self.window completionHandler:nil];
}

@end
