// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#import <os/log.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Unified log categories for SpaceHound's local-only diagnostics.
os_log_t SHLogLifecycle(void);
os_log_t SHLogPermissions(void);
os_log_t SHLogNavigation(void);
os_log_t SHLogSettings(void);
os_log_t SHLogUpdates(void);
os_log_t SHLogLoginItem(void);

#ifdef __cplusplus
}
#endif
