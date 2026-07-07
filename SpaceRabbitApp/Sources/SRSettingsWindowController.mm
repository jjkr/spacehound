#import "SRSettingsWindowController.h"

#import "SRSettingsStore.h"

namespace {

constexpr CGFloat hotkey_action_width = 240.0;
constexpr CGFloat hotkey_enabled_width = 74.0;
constexpr CGFloat hotkey_key_width = 90.0;

auto make_text_field(NSString *placeholder) -> NSTextField * {
  NSTextField *field = [[NSTextField alloc] initWithFrame:NSZeroRect];
  field.translatesAutoresizingMaskIntoConstraints = NO;
  field.placeholderString = placeholder;
  return field;
}

}  // namespace

@interface SRHotkeyRowView : NSView

@property(nonatomic, strong, readonly) SRHotkeyItem *item;

- (instancetype)initWithHotkeyItem:(SRHotkeyItem *)item;
- (SRHotkeyItem *)currentItem;

@end

@implementation SRHotkeyRowView {
  NSButton *_enabledButton;
  NSTextField *_keyField;
  NSTextField *_modifiersField;
}

- (instancetype)initWithHotkeyItem:(SRHotkeyItem *)item {
  self = [super initWithFrame:NSZeroRect];
  if (self == nil) {
    return nil;
  }

  _item = item;
  self.translatesAutoresizingMaskIntoConstraints = NO;

  NSTextField *actionLabel = [NSTextField labelWithString:item.displayName];
  actionLabel.translatesAutoresizingMaskIntoConstraints = NO;
  actionLabel.lineBreakMode = NSLineBreakByTruncatingTail;

  _enabledButton = [NSButton checkboxWithTitle:@"" target:nil action:nil];
  _enabledButton.translatesAutoresizingMaskIntoConstraints = NO;
  _enabledButton.state = item.enabled ? NSControlStateValueOn : NSControlStateValueOff;

  _keyField = make_text_field(@"a");
  _keyField.stringValue = item.key ?: @"";

  _modifiersField = make_text_field(@"option, shift");
  _modifiersField.stringValue = item.modifiersText ?: @"";

  [self addSubview:actionLabel];
  [self addSubview:_enabledButton];
  [self addSubview:_keyField];
  [self addSubview:_modifiersField];

  [NSLayoutConstraint activateConstraints:@[
    [actionLabel.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
    [actionLabel.topAnchor constraintEqualToAnchor:self.topAnchor constant:4.0],
    [actionLabel.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-4.0],
    [actionLabel.widthAnchor constraintEqualToConstant:hotkey_action_width],

    [_enabledButton.leadingAnchor constraintEqualToAnchor:actionLabel.trailingAnchor constant:12.0],
    [_enabledButton.centerYAnchor constraintEqualToAnchor:actionLabel.centerYAnchor],
    [_enabledButton.widthAnchor constraintEqualToConstant:hotkey_enabled_width],

    [_keyField.leadingAnchor constraintEqualToAnchor:_enabledButton.trailingAnchor constant:12.0],
    [_keyField.centerYAnchor constraintEqualToAnchor:actionLabel.centerYAnchor],
    [_keyField.widthAnchor constraintEqualToConstant:hotkey_key_width],

    [_modifiersField.leadingAnchor constraintEqualToAnchor:_keyField.trailingAnchor constant:12.0],
    [_modifiersField.centerYAnchor constraintEqualToAnchor:actionLabel.centerYAnchor],
    [_modifiersField.trailingAnchor constraintEqualToAnchor:self.trailingAnchor],
  ]];

  return self;
}

- (SRHotkeyItem *)currentItem {
  SRHotkeyItem *item = [[SRHotkeyItem alloc] init];
  item.actionID = self.item.actionID;
  item.sectionTitle = self.item.sectionTitle;
  item.displayName = self.item.displayName;
  item.enabled = (_enabledButton.state == NSControlStateValueOn);
  item.key = _keyField.stringValue ?: @"";
  item.modifiersText = _modifiersField.stringValue ?: @"";
  return item;
}

@end

@interface SRSettingsWindowController ()

@property(nonatomic, strong) NSTextField *settingsPathField;
@property(nonatomic, strong) NSButton *workspaceWrapButton;
@property(nonatomic, strong) NSButton *displayWrapButton;
@property(nonatomic, strong) NSButton *trayScrollButton;
@property(nonatomic, strong) NSButton *trayScrollInvertedButton;
@property(nonatomic, strong) NSButton *fastSwipeButton;
@property(nonatomic, strong) NSButton *telemetryButton;
@property(nonatomic, strong) NSStackView *hotkeysStackView;
@property(nonatomic, strong) NSTextField *statusLabel;
@property(nonatomic, copy) NSArray<SRHotkeyRowView *> *hotkeyRowViews;

- (void)reloadFromDisk:(id)sender;
- (void)saveSettings:(id)sender;
- (void)revealSettingsFile:(id)sender;
- (void)presentSettingsError:(NSError *)error;
- (void)trayScrollChanged:(id)sender;

@end

@implementation SRSettingsWindowController

- (instancetype)init {
  NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0.0, 0.0, 820.0, 760.0)
                                                 styleMask:(NSWindowStyleMaskTitled |
                                                            NSWindowStyleMaskClosable |
                                                            NSWindowStyleMaskMiniaturizable |
                                                            NSWindowStyleMaskResizable)
                                                   backing:NSBackingStoreBuffered
                                                     defer:NO];
  window.title = @"SpaceRabbit Settings";
  window.releasedWhenClosed = NO;
  window.minSize = NSMakeSize(720.0, 600.0);
  window.frameAutosaveName = @"SpaceRabbitSettingsWindow";

  self = [super initWithWindow:window];
  if (self == nil) {
    return nil;
  }

  [self buildInterface];
  return self;
}

- (void)showWindowAndActivate {
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
  rootStack.spacing = 16.0;

  NSTextField *introLabel = [NSTextField labelWithString:
      @"Edit the same settings.json file the runtime already uses. Unknown JSON fields stay on disk unchanged."];
  introLabel.translatesAutoresizingMaskIntoConstraints = NO;
  introLabel.lineBreakMode = NSLineBreakByWordWrapping;
  introLabel.maximumNumberOfLines = 0;

  self.settingsPathField = [NSTextField labelWithString:@""];
  self.settingsPathField.translatesAutoresizingMaskIntoConstraints = NO;
  self.settingsPathField.lineBreakMode = NSLineBreakByTruncatingMiddle;
  self.settingsPathField.allowsExpansionToolTips = YES;

  NSTextField *generalLabel = [self sectionLabelWithString:@"General"];

  self.workspaceWrapButton = [NSButton checkboxWithTitle:@"Wrap workspace navigation" target:nil action:nil];
  self.displayWrapButton = [NSButton checkboxWithTitle:@"Wrap display navigation" target:nil action:nil];
  self.trayScrollButton = [NSButton checkboxWithTitle:@"Enable tray scroll switching" target:self action:@selector(trayScrollChanged:)];
  self.trayScrollInvertedButton = [NSButton checkboxWithTitle:@"Invert tray scroll direction" target:nil action:nil];
  self.fastSwipeButton = [NSButton checkboxWithTitle:@"Enable fast swipe" target:nil action:nil];
  self.telemetryButton = [NSButton checkboxWithTitle:@"Enable telemetry" target:nil action:nil];

  NSStackView *generalStack = [[NSStackView alloc] initWithFrame:NSZeroRect];
  generalStack.translatesAutoresizingMaskIntoConstraints = NO;
  generalStack.orientation = NSUserInterfaceLayoutOrientationVertical;
  generalStack.alignment = NSLayoutAttributeLeading;
  generalStack.spacing = 8.0;
  [generalStack addArrangedSubview:self.workspaceWrapButton];
  [generalStack addArrangedSubview:self.displayWrapButton];
  [generalStack addArrangedSubview:self.trayScrollButton];
  [generalStack addArrangedSubview:self.trayScrollInvertedButton];
  [generalStack addArrangedSubview:self.fastSwipeButton];
  [generalStack addArrangedSubview:self.telemetryButton];

  NSTextField *hotkeysLabel = [self sectionLabelWithString:@"Hotkeys"];
  NSTextField *hotkeysHelpLabel = [NSTextField labelWithString:
      @"Use comma or + separated modifiers, for example option, shift or ctrl+cmd."];
  hotkeysHelpLabel.translatesAutoresizingMaskIntoConstraints = NO;
  hotkeysHelpLabel.textColor = [NSColor secondaryLabelColor];

  NSView *headerRow = [self hotkeyHeaderRow];

  self.hotkeysStackView = [[NSStackView alloc] initWithFrame:NSZeroRect];
  self.hotkeysStackView.translatesAutoresizingMaskIntoConstraints = NO;
  self.hotkeysStackView.orientation = NSUserInterfaceLayoutOrientationVertical;
  self.hotkeysStackView.alignment = NSLayoutAttributeLeading;
  self.hotkeysStackView.spacing = 10.0;

  NSScrollView *scrollView = [[NSScrollView alloc] initWithFrame:NSZeroRect];
  scrollView.translatesAutoresizingMaskIntoConstraints = NO;
  scrollView.hasVerticalScroller = YES;
  scrollView.borderType = NSBezelBorder;
  scrollView.documentView = self.hotkeysStackView;

  self.statusLabel = [NSTextField labelWithString:@""];
  self.statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
  self.statusLabel.textColor = [NSColor secondaryLabelColor];

  NSButton *revealButton = [NSButton buttonWithTitle:@"Reveal JSON" target:self action:@selector(revealSettingsFile:)];
  NSButton *reloadButton = [NSButton buttonWithTitle:@"Reload" target:self action:@selector(reloadFromDisk:)];
  NSButton *saveButton = [NSButton buttonWithTitle:@"Save" target:self action:@selector(saveSettings:)];
  saveButton.keyEquivalent = @"\r";
  saveButton.bezelStyle = NSBezelStyleRounded;

  NSStackView *buttonRow = [[NSStackView alloc] initWithFrame:NSZeroRect];
  buttonRow.translatesAutoresizingMaskIntoConstraints = NO;
  buttonRow.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  buttonRow.alignment = NSLayoutAttributeCenterY;
  buttonRow.spacing = 8.0;
  [buttonRow addArrangedSubview:revealButton];
  [buttonRow addArrangedSubview:reloadButton];
  [buttonRow addArrangedSubview:[self flexibleSpacer]];
  [buttonRow addArrangedSubview:saveButton];

  [rootStack addArrangedSubview:introLabel];
  [rootStack addArrangedSubview:self.settingsPathField];
  [rootStack addArrangedSubview:generalLabel];
  [rootStack addArrangedSubview:generalStack];
  [rootStack addArrangedSubview:hotkeysLabel];
  [rootStack addArrangedSubview:hotkeysHelpLabel];
  [rootStack addArrangedSubview:headerRow];
  [rootStack addArrangedSubview:scrollView];
  [rootStack addArrangedSubview:self.statusLabel];
  [rootStack addArrangedSubview:buttonRow];

  [contentView addSubview:rootStack];

  [NSLayoutConstraint activateConstraints:@[
    [rootStack.leadingAnchor constraintEqualToAnchor:contentView.leadingAnchor constant:20.0],
    [rootStack.trailingAnchor constraintEqualToAnchor:contentView.trailingAnchor constant:-20.0],
    [rootStack.topAnchor constraintEqualToAnchor:contentView.topAnchor constant:20.0],
    [rootStack.bottomAnchor constraintEqualToAnchor:contentView.bottomAnchor constant:-20.0],
    [scrollView.heightAnchor constraintGreaterThanOrEqualToConstant:300.0],
    [self.hotkeysStackView.widthAnchor constraintEqualToAnchor:scrollView.contentView.widthAnchor],
  ]];
}

- (NSTextField *)sectionLabelWithString:(NSString *)stringValue {
  NSTextField *label = [NSTextField labelWithString:stringValue];
  label.translatesAutoresizingMaskIntoConstraints = NO;
  label.font = [NSFont boldSystemFontOfSize:13.0];
  return label;
}

- (NSView *)hotkeyHeaderRow {
  NSView *header = [[NSView alloc] initWithFrame:NSZeroRect];
  header.translatesAutoresizingMaskIntoConstraints = NO;

  NSTextField *actionLabel = [NSTextField labelWithString:@"Action"];
  NSTextField *enabledLabel = [NSTextField labelWithString:@"Enabled"];
  NSTextField *keyLabel = [NSTextField labelWithString:@"Key"];
  NSTextField *modifiersLabel = [NSTextField labelWithString:@"Modifiers"];

  for (NSTextField *label in @[actionLabel, enabledLabel, keyLabel, modifiersLabel]) {
    label.translatesAutoresizingMaskIntoConstraints = NO;
    label.font = [NSFont boldSystemFontOfSize:12.0];
    [header addSubview:label];
  }

  [NSLayoutConstraint activateConstraints:@[
    [actionLabel.leadingAnchor constraintEqualToAnchor:header.leadingAnchor],
    [actionLabel.topAnchor constraintEqualToAnchor:header.topAnchor],
    [actionLabel.bottomAnchor constraintEqualToAnchor:header.bottomAnchor],
    [actionLabel.widthAnchor constraintEqualToConstant:hotkey_action_width],

    [enabledLabel.leadingAnchor constraintEqualToAnchor:actionLabel.trailingAnchor constant:12.0],
    [enabledLabel.topAnchor constraintEqualToAnchor:header.topAnchor],
    [enabledLabel.bottomAnchor constraintEqualToAnchor:header.bottomAnchor],
    [enabledLabel.widthAnchor constraintEqualToConstant:hotkey_enabled_width],

    [keyLabel.leadingAnchor constraintEqualToAnchor:enabledLabel.trailingAnchor constant:12.0],
    [keyLabel.topAnchor constraintEqualToAnchor:header.topAnchor],
    [keyLabel.bottomAnchor constraintEqualToAnchor:header.bottomAnchor],
    [keyLabel.widthAnchor constraintEqualToConstant:hotkey_key_width],

    [modifiersLabel.leadingAnchor constraintEqualToAnchor:keyLabel.trailingAnchor constant:12.0],
    [modifiersLabel.topAnchor constraintEqualToAnchor:header.topAnchor],
    [modifiersLabel.bottomAnchor constraintEqualToAnchor:header.bottomAnchor],
    [modifiersLabel.trailingAnchor constraintEqualToAnchor:header.trailingAnchor],
  ]];

  return header;
}

- (NSView *)flexibleSpacer {
  NSView *spacer = [[NSView alloc] initWithFrame:NSZeroRect];
  spacer.translatesAutoresizingMaskIntoConstraints = NO;
  [spacer.widthAnchor constraintGreaterThanOrEqualToConstant:12.0].active = YES;
  [spacer setContentHuggingPriority:NSLayoutPriorityDefaultLow forOrientation:NSLayoutConstraintOrientationHorizontal];
  [spacer setContentCompressionResistancePriority:NSLayoutPriorityDefaultLow
                                   forOrientation:NSLayoutConstraintOrientationHorizontal];
  return spacer;
}

- (void)reloadFromDisk:(id)sender {
  (void)sender;

  NSError *pathError = nil;
  NSURL *settingsURL = [SRSettingsStore settingsFileURL:&pathError];
  if (settingsURL == nil) {
    [self presentSettingsError:pathError ?: [NSError errorWithDomain:NSCocoaErrorDomain
                                                                code:NSFileReadUnknownError
                                                            userInfo:@{NSLocalizedDescriptionKey : @"Failed to locate settings.json."}]];
    return;
  }

  NSError *loadError = nil;
  SRSettingsDocument *document = [SRSettingsStore loadDocument:&loadError];
  if (document == nil) {
    [self presentSettingsError:loadError ?: [NSError errorWithDomain:NSCocoaErrorDomain
                                                                code:NSFileReadUnknownError
                                                            userInfo:@{NSLocalizedDescriptionKey : @"Failed to load settings.json."}]];
    return;
  }

  self.settingsPathField.stringValue = settingsURL.path ?: @"";
  [self applyDocumentToControls:document];
  self.statusLabel.stringValue = @"";
}

- (void)saveSettings:(id)sender {
  (void)sender;

  SRSettingsDocument *document = [[SRSettingsDocument alloc] init];
  document.version = @"1.0";
  document.workspaceWrap = (self.workspaceWrapButton.state == NSControlStateValueOn);
  document.displayWrap = (self.displayWrapButton.state == NSControlStateValueOn);
  document.trayScroll = (self.trayScrollButton.state == NSControlStateValueOn);
  document.trayScrollInverted = (self.trayScrollInvertedButton.state == NSControlStateValueOn);
  document.fastSwipe = (self.fastSwipeButton.state == NSControlStateValueOn);
  document.telemetryEnabled = (self.telemetryButton.state == NSControlStateValueOn);

  NSMutableArray<SRHotkeyItem *> *hotkeys = [NSMutableArray arrayWithCapacity:self.hotkeyRowViews.count];
  for (SRHotkeyRowView *rowView in self.hotkeyRowViews) {
    [hotkeys addObject:[rowView currentItem]];
  }
  document.hotkeys = [hotkeys copy];

  NSError *saveError = nil;
  if (![SRSettingsStore saveDocument:document error:&saveError]) {
    [self presentSettingsError:saveError ?: [NSError errorWithDomain:NSCocoaErrorDomain
                                                                code:NSFileWriteUnknownError
                                                            userInfo:@{NSLocalizedDescriptionKey : @"Failed to save settings.json."}]];
    return;
  }

  if (self.applyHandler != nil) {
    NSError *applyError = nil;
    if (!self.applyHandler(&applyError)) {
      self.statusLabel.stringValue = @"Saved to disk, but the running runtime could not apply the update.";
      if (applyError != nil) {
        [self presentSettingsError:applyError];
      }
      return;
    }
  }

  self.statusLabel.stringValue = @"Saved and applied.";
}

- (void)revealSettingsFile:(id)sender {
  (void)sender;

  NSError *error = nil;
  NSURL *settingsURL = [SRSettingsStore settingsFileURL:&error];
  if (settingsURL == nil) {
    [self presentSettingsError:error ?: [NSError errorWithDomain:NSCocoaErrorDomain
                                                            code:NSFileNoSuchFileError
                                                        userInfo:@{NSLocalizedDescriptionKey : @"Failed to locate settings.json."}]];
    return;
  }

  [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[settingsURL]];
}

- (void)trayScrollChanged:(id)sender {
  (void)sender;
  BOOL trayScrollEnabled = (self.trayScrollButton.state == NSControlStateValueOn);
  self.trayScrollInvertedButton.enabled = trayScrollEnabled;
  if (!trayScrollEnabled) {
    self.trayScrollInvertedButton.state = NSControlStateValueOff;
  }
}

- (void)applyDocumentToControls:(SRSettingsDocument *)document {
  self.workspaceWrapButton.state = document.workspaceWrap ? NSControlStateValueOn : NSControlStateValueOff;
  self.displayWrapButton.state = document.displayWrap ? NSControlStateValueOn : NSControlStateValueOff;
  self.trayScrollButton.state = document.trayScroll ? NSControlStateValueOn : NSControlStateValueOff;
  self.trayScrollInvertedButton.state =
      document.trayScrollInverted ? NSControlStateValueOn : NSControlStateValueOff;
  self.fastSwipeButton.state = document.fastSwipe ? NSControlStateValueOn : NSControlStateValueOff;
  self.telemetryButton.state = document.telemetryEnabled ? NSControlStateValueOn : NSControlStateValueOff;
  [self trayScrollChanged:nil];

  for (NSView *view in [self.hotkeysStackView.arrangedSubviews copy]) {
    [self.hotkeysStackView removeArrangedSubview:view];
    [view removeFromSuperview];
  }

  NSMutableArray<SRHotkeyRowView *> *rowViews = [NSMutableArray arrayWithCapacity:document.hotkeys.count];
  NSString *currentSection = nil;
  for (SRHotkeyItem *item in document.hotkeys) {
    if (![currentSection isEqualToString:item.sectionTitle]) {
      currentSection = item.sectionTitle;
      NSTextField *sectionLabel = [self sectionLabelWithString:currentSection];
      sectionLabel.font = [NSFont systemFontOfSize:12.0 weight:NSFontWeightSemibold];
      [self.hotkeysStackView addArrangedSubview:sectionLabel];
    }

    SRHotkeyRowView *rowView = [[SRHotkeyRowView alloc] initWithHotkeyItem:item];
    [rowViews addObject:rowView];
    [self.hotkeysStackView addArrangedSubview:rowView];
  }

  self.hotkeyRowViews = [rowViews copy];
}

- (void)presentSettingsError:(NSError *)error {
  NSAlert *alert = [[NSAlert alloc] init];
  alert.alertStyle = NSAlertStyleWarning;
  alert.messageText = @"SpaceRabbit Settings";
  alert.informativeText = error.localizedDescription ?: @"An unknown error occurred.";
  [alert beginSheetModalForWindow:self.window completionHandler:nil];
}

@end
