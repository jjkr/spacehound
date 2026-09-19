#pragma once

#include <ApplicationServices/ApplicationServices.h>

#include <cstdint>
#include <cstring>
#include <sys/types.h>

#include <spacehound/cf.hpp>

extern "C" {
extern CGError _SLPSSetFrontProcessWithOptions(
    ProcessSerialNumber *psn,
    std::uint32_t window_id,
    std::uint32_t mode);
extern CGError SLPSPostEventRecordTo(ProcessSerialNumber *psn, std::uint8_t *bytes);
extern int CGSMainConnectionID(void);
extern CFArrayRef CGSCopyManagedDisplaySpaces(int connection_id);
extern CFStringRef CGSCopyActiveMenuBarDisplayIdentifier(int connection_id);
extern CFArrayRef CGSCopySpacesForWindows(
    int connection_id,
    std::uint32_t selector,
    CFArrayRef window_ids);
extern CGError CGSAddWindowsToSpaces(
    int connection_id,
    CFArrayRef window_ids,
    CFArrayRef space_ids);
extern CGError CGSRemoveWindowsFromSpaces(
    int connection_id,
    CFArrayRef window_ids,
    CFArrayRef space_ids);
extern void SLSSetActiveMenuBarDisplayIdentifier(CFStringRef display_identifier);
}

namespace spacehound::cgs {

using connection_id = int;
using space_selector = std::uint32_t;

/// Returns the process Core Graphics Services connection id via `CGSMainConnectionID`.
[[nodiscard]] inline auto main_connection_id() noexcept -> connection_id {
  return CGSMainConnectionID();
}

/// `_SLPSSetFrontProcessWithOptions` mode flag for a user-initiated switch.
inline constexpr std::uint32_t front_process_user_generated = 0x200;

/// Brings `window_id` of process `pid` to the front through the window server
/// directly: `_SLPSSetFrontProcessWithOptions` followed by the pair of
/// synthetic key-window event records that make the process treat the window
/// as key. This is what tiling window managers use; it completes in a
/// millisecond or two where `-[NSRunningApplication activateWithOptions:]`
/// round-trips through LaunchServices and the target app.
[[nodiscard]] inline auto set_front_window(pid_t pid, std::uint32_t window_id) noexcept -> bool {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  ProcessSerialNumber psn{};
  if (GetProcessForPID(pid, &psn) != noErr) {
    return false;
  }
#pragma clang diagnostic pop

  if (_SLPSSetFrontProcessWithOptions(&psn, window_id, front_process_user_generated) !=
      kCGErrorSuccess) {
    return false;
  }

  // Event record layout as used by yabai's window_manager_make_key_window:
  // 0xF8-byte record, event kind at 0x08 (1 = key-window in, 2 = key-window
  // out), 0x10 at 0x3a, 0xFF fill at 0x20..0x30, window id at 0x3c.
  for (const std::uint8_t kind : {std::uint8_t{1}, std::uint8_t{2}}) {
    std::uint8_t record[0xf8] = {};
    record[0x04] = 0xF8;
    record[0x08] = kind;
    record[0x3a] = 0x10;
    std::memset(record + 0x20, 0xFF, 0x10);
    std::memcpy(record + 0x3c, &window_id, sizeof(window_id));
    if (SLPSPostEventRecordTo(&psn, record) != kCGErrorSuccess) {
      return false;
    }
  }

  return true;
}

/// Copies the managed display/space model via `CGSCopyManagedDisplaySpaces`.
[[nodiscard]] inline auto copy_managed_display_spaces(connection_id connection) noexcept
    -> cf::retained<CFArrayRef> {
  return cf::adopt(CGSCopyManagedDisplaySpaces(connection));
}

/// Copies the active menu-bar display identifier via `CGSCopyActiveMenuBarDisplayIdentifier`.
[[nodiscard]] inline auto copy_active_menu_bar_display_identifier(
    connection_id connection) noexcept -> cf::string {
  return cf::string::adopt(CGSCopyActiveMenuBarDisplayIdentifier(connection));
}

/// Copies the spaces associated with a set of windows via `CGSCopySpacesForWindows`.
[[nodiscard]] inline auto copy_spaces_for_windows(
    connection_id connection,
    space_selector selector,
    cf::view<CFArrayRef> window_ids) noexcept -> cf::retained<CFArrayRef> {
  return cf::adopt(CGSCopySpacesForWindows(connection, selector, window_ids.get()));
}

/// Adds windows to spaces via `CGSAddWindowsToSpaces`.
[[nodiscard]] inline auto add_windows_to_spaces(
    connection_id connection,
    cf::view<CFArrayRef> window_ids,
    cf::view<CFArrayRef> space_ids) noexcept -> CGError {
  return CGSAddWindowsToSpaces(connection, window_ids.get(), space_ids.get());
}

/// Removes windows from spaces via `CGSRemoveWindowsFromSpaces`.
[[nodiscard]] inline auto remove_windows_from_spaces(
    connection_id connection,
    cf::view<CFArrayRef> window_ids,
    cf::view<CFArrayRef> space_ids) noexcept -> CGError {
  return CGSRemoveWindowsFromSpaces(connection, window_ids.get(), space_ids.get());
}

/// Sets the active menu-bar display identifier via `SLSSetActiveMenuBarDisplayIdentifier`.
inline void set_active_menu_bar_display_identifier(cf::string_view display_identifier) noexcept {
  SLSSetActiveMenuBarDisplayIdentifier(display_identifier.get());
}

}  // namespace spacehound::cgs
