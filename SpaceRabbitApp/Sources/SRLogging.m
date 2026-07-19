#import "SRLogging.h"

static const char *const SRLoggingSubsystem = "com.animaslabs.SpaceRabbit";

#define SR_DEFINE_LOG_CATEGORY(functionName, categoryName) \
  os_log_t functionName(void) {                            \
    static os_log_t log = nil;                             \
    static dispatch_once_t onceToken;                      \
    dispatch_once(&onceToken, ^{                           \
      log = os_log_create(SRLoggingSubsystem, categoryName); \
    });                                                    \
    return log;                                            \
  }

SR_DEFINE_LOG_CATEGORY(SRLogLifecycle, "lifecycle")
SR_DEFINE_LOG_CATEGORY(SRLogPermissions, "permissions")
SR_DEFINE_LOG_CATEGORY(SRLogNavigation, "navigation")
SR_DEFINE_LOG_CATEGORY(SRLogSettings, "settings")
SR_DEFINE_LOG_CATEGORY(SRLogUpdates, "updates")
SR_DEFINE_LOG_CATEGORY(SRLogLoginItem, "login-item")
