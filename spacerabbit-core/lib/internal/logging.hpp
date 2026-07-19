#pragma once

#include <os/log.h>

namespace spacerabbit::diagnostics {

inline constexpr auto subsystem = "com.animaslabs.SpaceRabbit";

inline auto lifecycle_log() noexcept -> os_log_t {
  static os_log_t log = os_log_create(subsystem, "lifecycle");
  return log;
}

inline auto navigation_log() noexcept -> os_log_t {
  static os_log_t log = os_log_create(subsystem, "navigation");
  return log;
}

inline auto settings_log() noexcept -> os_log_t {
  static os_log_t log = os_log_create(subsystem, "settings");
  return log;
}

}  // namespace spacerabbit::diagnostics
