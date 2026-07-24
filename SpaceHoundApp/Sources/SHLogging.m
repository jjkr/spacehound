#import "SHLogging.h"

static const char *const SHLoggingSubsystem = "com.animaslabs.SpaceHound";

#define SH_DEFINE_LOG_CATEGORY(functionName, categoryName) \
  os_log_t functionName(void) {                            \
    static os_log_t log = nil;                             \
    static dispatch_once_t onceToken;                      \
    dispatch_once(&onceToken, ^{                           \
      log = os_log_create(SHLoggingSubsystem, categoryName); \
    });                                                    \
    return log;                                            \
  }

SH_DEFINE_LOG_CATEGORY(SHLogLifecycle, "lifecycle")
SH_DEFINE_LOG_CATEGORY(SHLogPermissions, "permissions")
SH_DEFINE_LOG_CATEGORY(SHLogNavigation, "navigation")
SH_DEFINE_LOG_CATEGORY(SHLogSettings, "settings")
SH_DEFINE_LOG_CATEGORY(SHLogUpdates, "updates")
SH_DEFINE_LOG_CATEGORY(SHLogLoginItem, "login-item")
