// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// YES when a Sentry DSN is configured, so crash reporting can be offered.
BOOL SHCrashReportingIsAvailable(void);

/// Starts privacy-limited crash reporting when a DSN is configured and the
/// user has opted in. Safe to call repeatedly; a running SDK is left alone.
void SHStartCrashReportingIfEnabled(void);

/// Stops crash reporting if it is running.
void SHStopCrashReporting(void);

/// Crashes the process immediately so the crash pipeline can be verified. The
/// report is uploaded on the next launch. No report is captured while a
/// debugger is attached.
void SHCrashForTesting(void);

NS_ASSUME_NONNULL_END
