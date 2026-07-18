#import "SRLoginItemManager.h"

#import <ServiceManagement/ServiceManagement.h>

@implementation SRLoginItemManager

+ (SRLoginItemStatus)status {
  switch (SMAppService.mainAppService.status) {
    case SMAppServiceStatusNotRegistered:
      return SRLoginItemStatusNotRegistered;
    case SMAppServiceStatusEnabled:
      return SRLoginItemStatusEnabled;
    case SMAppServiceStatusRequiresApproval:
      return SRLoginItemStatusRequiresApproval;
    case SMAppServiceStatusNotFound:
      return SRLoginItemStatusNotFound;
  }

  return SRLoginItemStatusNotFound;
}

+ (BOOL)setEnabled:(BOOL)enabled error:(NSError **)error {
  SMAppService *service = SMAppService.mainAppService;
  const SMAppServiceStatus status = service.status;

  if (enabled) {
    if (status == SMAppServiceStatusEnabled || status == SMAppServiceStatusRequiresApproval) {
      return YES;
    }
    return [service registerAndReturnError:error];
  }

  if (status == SMAppServiceStatusNotRegistered || status == SMAppServiceStatusNotFound) {
    return YES;
  }
  return [service unregisterAndReturnError:error];
}

+ (void)openSystemSettings {
  [SMAppService openSystemSettingsLoginItems];
}

@end
