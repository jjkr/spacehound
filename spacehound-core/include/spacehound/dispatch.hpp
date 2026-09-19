// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <dispatch/dispatch.h>

#include <cstdint>

namespace spacehound::dispatch {

using function = void (*)(void *);

/// Returns the process main queue backed by `_dispatch_main_q`.
[[nodiscard]] inline auto main_queue() noexcept -> dispatch_queue_t {
  return reinterpret_cast<dispatch_queue_t>(&_dispatch_main_q);
}

/// Dispatches a function to the main queue via `dispatch_async_f`.
inline void to_main(function callback, void *context) noexcept {
  dispatch_async_f(main_queue(), context, callback);
}

/// Runs `callback` on the main queue after `delay_ms` milliseconds.
inline void to_main_after_ms(long long delay_ms, function callback, void *context) noexcept {
  const auto delay_ns = static_cast<std::int64_t>(delay_ms) * static_cast<std::int64_t>(NSEC_PER_MSEC);
  dispatch_after_f(dispatch_time(DISPATCH_TIME_NOW, delay_ns), main_queue(), context, callback);
}

}  // namespace spacehound::dispatch
