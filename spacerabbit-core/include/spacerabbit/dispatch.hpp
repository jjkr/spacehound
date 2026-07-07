#pragma once

#include <dispatch/queue.h>

namespace spacerabbit::dispatch {

using function = void (*)(void *);

/// Returns the process main queue backed by `_dispatch_main_q`.
[[nodiscard]] inline auto main_queue() noexcept -> dispatch_queue_t {
  return reinterpret_cast<dispatch_queue_t>(&_dispatch_main_q);
}

/// Dispatches a function to the main queue via `dispatch_async_f`.
inline void to_main(function callback, void *context) noexcept {
  dispatch_async_f(main_queue(), context, callback);
}

}  // namespace spacerabbit::dispatch
