#import "SRDaemonSupervisor.h"

static NSString *const SRDaemonPathOverrideEnvironmentVariable = @"SPACERABBITD_PATH";

@interface SRDaemonSupervisor ()

@property(nonatomic, copy) NSString *statusText;
@property(nonatomic, strong, nullable) NSTask *task;
@property(nonatomic, strong, nullable) NSPipe *outputPipe;
@property(nonatomic, assign) BOOL shouldKeepRunning;
@property(nonatomic, assign) NSUInteger consecutiveRestartCount;

@end

@implementation SRDaemonSupervisor

- (instancetype)init {
  self = [super init];
  if (self == nil) {
    return nil;
  }

  _statusText = @"Daemon: Stopped";
  return self;
}

- (void)start {
  self.shouldKeepRunning = YES;

  if (self.task != nil && self.task.running) {
    [self updateStatusText:@"Daemon: Running"];
    return;
  }

  [self launchDaemon];
}

- (void)restart {
  self.shouldKeepRunning = YES;

  if (self.task != nil && self.task.running) {
    [self.task terminate];
    return;
  }

  [self launchDaemon];
}

- (void)stop {
  self.shouldKeepRunning = NO;
  self.consecutiveRestartCount = 0;

  if (self.task != nil) {
    self.task.terminationHandler = nil;
    if (self.task.running) {
      [self.task terminate];
    }
  }

  [self teardownTaskIO];
  self.task = nil;
  [self updateStatusText:@"Daemon: Stopped"];
}

- (void)launchDaemon {
  NSURL *daemonURL = [self resolvedDaemonURL];
  if (daemonURL == nil) {
    [self updateStatusText:@"Daemon: Binary not found"];
    return;
  }

  NSURL *settingsURL = nil;
  NSError *settingsError = nil;
  if (![self ensureDefaultSettingsFileExists:&settingsURL error:&settingsError]) {
    NSString *message = settingsError.localizedDescription ?: @"Failed to prepare settings";
    [self updateStatusText:[NSString stringWithFormat:@"Daemon: %@", message]];
    return;
  }

  NSPipe *pipe = [NSPipe pipe];
  [self attachLoggingToPipe:pipe daemonPath:daemonURL.path];

  NSTask *task = [[NSTask alloc] init];
  task.executableURL = daemonURL;
  task.arguments = @[ @"--settings", settingsURL.path ];
  task.standardOutput = pipe;
  task.standardError = pipe;

  __weak typeof(self) weakSelf = self;
  task.terminationHandler = ^(NSTask *terminatedTask) {
    dispatch_async(dispatch_get_main_queue(), ^{
      [weakSelf handleTaskTermination:terminatedTask];
    });
  };

  NSError *launchError = nil;
  if (![task launchAndReturnError:&launchError]) {
    [self teardownTaskIO];
    NSString *message = launchError.localizedDescription ?: @"Failed to launch daemon";
    [self updateStatusText:[NSString stringWithFormat:@"Daemon: %@", message]];
    return;
  }

  self.task = task;
  self.consecutiveRestartCount = 0;
  [self updateStatusText:@"Daemon: Running"];
}

- (void)handleTaskTermination:(NSTask *)terminatedTask {
  if (terminatedTask != self.task) {
    return;
  }

  self.task = nil;
  [self teardownTaskIO];

  if (!self.shouldKeepRunning) {
    [self updateStatusText:@"Daemon: Stopped"];
    return;
  }

  if ([self shouldRestartAfterTermination:terminatedTask]) {
    self.consecutiveRestartCount += 1;
    NSTimeInterval restartDelay = MIN((NSTimeInterval)self.consecutiveRestartCount, 5.0);
    [self updateStatusText:[NSString stringWithFormat:@"Daemon: Restarting in %.0fs", restartDelay]];

    __weak typeof(self) weakSelf = self;
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)(restartDelay * NSEC_PER_SEC)),
        dispatch_get_main_queue(),
        ^{
          if (!weakSelf.shouldKeepRunning || weakSelf.task != nil) {
            return;
          }

          [weakSelf launchDaemon];
        });
    return;
  }

  [self updateStatusText:[self statusTextForStoppedTask:terminatedTask]];
}

- (BOOL)shouldRestartAfterTermination:(NSTask *)terminatedTask {
  if (terminatedTask.terminationReason == NSTaskTerminationReasonUncaughtSignal) {
    return YES;
  }

  switch (terminatedTask.terminationStatus) {
    case 0:
      return YES;
    case 1:
    case 2:
    case 3:
      return NO;
    default:
      return YES;
  }
}

- (NSString *)statusTextForStoppedTask:(NSTask *)terminatedTask {
  if (terminatedTask.terminationReason == NSTaskTerminationReasonUncaughtSignal) {
    return [NSString stringWithFormat:@"Daemon: Crashed (signal %d)", terminatedTask.terminationStatus];
  }

  switch (terminatedTask.terminationStatus) {
    case 0:
      return @"Daemon: Stopped";
    case 1:
      return @"Daemon: Invalid launch arguments";
    case 2:
      return @"Daemon: Settings error";
    case 3:
      return @"Daemon: Permission required";
    case 4:
      return @"Daemon: Runtime error";
    default:
      return [NSString stringWithFormat:@"Daemon: Exited (%d)", terminatedTask.terminationStatus];
  }
}

- (void)attachLoggingToPipe:(NSPipe *)pipe daemonPath:(NSString *)daemonPath {
  self.outputPipe = pipe;

  __weak typeof(self) weakSelf = self;
  pipe.fileHandleForReading.readabilityHandler = ^(NSFileHandle *handle) {
    NSData *data = handle.availableData;
    if (data.length == 0) {
      return;
    }

    NSString *output = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (output.length == 0) {
      return;
    }

    NSLog(@"spacerabbitd (%@): %@", daemonPath, [output stringByTrimmingCharactersInSet:[NSCharacterSet newlineCharacterSet]]);
    (void)weakSelf;
  };
}

- (void)teardownTaskIO {
  if (self.outputPipe == nil) {
    return;
  }

  self.outputPipe.fileHandleForReading.readabilityHandler = nil;
  self.outputPipe = nil;
}

- (nullable NSURL *)resolvedDaemonURL {
  NSFileManager *fileManager = [NSFileManager defaultManager];
  NSString *environmentOverride = NSProcessInfo.processInfo.environment[SRDaemonPathOverrideEnvironmentVariable];
  if (environmentOverride.length > 0 && [fileManager isExecutableFileAtPath:environmentOverride]) {
    return [NSURL fileURLWithPath:environmentOverride];
  }

  NSString *bundledPath =
      [[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:@"Contents/Helpers/spacerabbitd"];
  if ([fileManager isExecutableFileAtPath:bundledPath]) {
    return [NSURL fileURLWithPath:bundledPath];
  }

  return nil;
}

- (BOOL)ensureDefaultSettingsFileExists:(NSURL *_Nullable *_Nullable)settingsURL
                                  error:(NSError *_Nullable *_Nullable)error {
  NSFileManager *fileManager = [NSFileManager defaultManager];
  NSURL *applicationSupportDirectory =
      [[fileManager URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask] firstObject];
  if (applicationSupportDirectory == nil) {
    if (error != NULL) {
      *error = [NSError errorWithDomain:NSCocoaErrorDomain
                                   code:NSFileNoSuchFileError
                               userInfo:@{NSLocalizedDescriptionKey : @"Application Support directory not found"}];
    }
    return NO;
  }

  NSURL *spaceRabbitDirectory = [applicationSupportDirectory URLByAppendingPathComponent:@"SpaceRabbit"
                                                                              isDirectory:YES];
  if (![fileManager createDirectoryAtURL:spaceRabbitDirectory
             withIntermediateDirectories:YES
                              attributes:nil
                                   error:error]) {
    return NO;
  }

  NSURL *resolvedSettingsURL = [spaceRabbitDirectory URLByAppendingPathComponent:@"settings.json"];
  if (![fileManager fileExistsAtPath:resolvedSettingsURL.path]) {
    NSDictionary *defaultSettings = @{
      @"version" : @"1.0",
      @"workspaceWrap" : @NO,
      @"displayWrap" : @NO,
      @"trayScroll" : @YES,
      @"trayScrollInverted" : @NO,
      @"hotkeys" : @{},
      @"fastSwipe" : @YES,
      @"telemetryEnabled" : @YES,
    };
    NSData *settingsData = [NSJSONSerialization dataWithJSONObject:defaultSettings
                                                           options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys
                                                             error:error];
    if (settingsData == nil) {
      return NO;
    }

    if (![settingsData writeToURL:resolvedSettingsURL options:NSDataWritingAtomic error:error]) {
      return NO;
    }
  }

  if (settingsURL != NULL) {
    *settingsURL = resolvedSettingsURL;
  }

  return YES;
}

- (void)updateStatusText:(NSString *)statusText {
  _statusText = [statusText copy];

  if (self.statusChangeHandler != nil) {
    self.statusChangeHandler(_statusText);
  }
}

@end
