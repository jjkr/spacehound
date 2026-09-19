// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#import "SHLoginItemManager.h"

#import "SHLogging.h"

#import <ServiceManagement/ServiceManagement.h>

@implementation SHLoginItemManager

+ (SHLoginItemStatus)status {
  switch (SMAppService.mainAppService.status) {
    case SMAppServiceStatusNotRegistered:
      return SHLoginItemStatusNotRegistered;
    case SMAppServiceStatusEnabled:
      return SHLoginItemStatusEnabled;
    case SMAppServiceStatusRequiresApproval:
      return SHLoginItemStatusRequiresApproval;
    case SMAppServiceStatusNotFound:
      return SHLoginItemStatusNotFound;
  }

  return SHLoginItemStatusNotFound;
}

+ (BOOL)setEnabled:(BOOL)enabled error:(NSError **)error {
  SMAppService *service = SMAppService.mainAppService;
  const SMAppServiceStatus status = service.status;

  if (enabled) {
    if (status == SMAppServiceStatusEnabled || status == SMAppServiceStatusRequiresApproval) {
      os_log_debug(SHLogLoginItem(), "Launch at login is already registered");
      return YES;
    }

    NSError *registerError = nil;
    const BOOL registered = [service registerAndReturnError:&registerError];
    if (!registered) {
      if (error != NULL) {
        *error = registerError;
      }
      os_log_error(SHLogLoginItem(),
                   "Failed to register launch at login (domain=%{private}@ code=%{public}ld)",
                   registerError.domain,
                   (long)registerError.code);
      return NO;
    }

    os_log_info(SHLogLoginItem(), "Launch at login registered");
    return YES;
  }

  if (status == SMAppServiceStatusNotRegistered || status == SMAppServiceStatusNotFound) {
    os_log_debug(SHLogLoginItem(), "Launch at login is already unregistered");
    return YES;
  }

  NSError *unregisterError = nil;
  const BOOL unregistered = [service unregisterAndReturnError:&unregisterError];
  if (!unregistered) {
    if (error != NULL) {
      *error = unregisterError;
    }
    os_log_error(SHLogLoginItem(),
                 "Failed to unregister launch at login (domain=%{private}@ code=%{public}ld)",
                 unregisterError.domain,
                 (long)unregisterError.code);
    return NO;
  }

  os_log_info(SHLogLoginItem(), "Launch at login unregistered");
  return YES;
}

+ (void)openSystemSettings {
  os_log_info(SHLogLoginItem(), "Opening Login Items settings");
  [SMAppService openSystemSettingsLoginItems];
}

@end
