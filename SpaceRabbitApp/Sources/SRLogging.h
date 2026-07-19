#import <os/log.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Unified log categories for SpaceRabbit's local-only diagnostics.
os_log_t SRLogLifecycle(void);
os_log_t SRLogPermissions(void);
os_log_t SRLogNavigation(void);
os_log_t SRLogSettings(void);
os_log_t SRLogUpdates(void);
os_log_t SRLogLoginItem(void);

#ifdef __cplusplus
}
#endif
