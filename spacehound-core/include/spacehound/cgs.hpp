#pragma once

#include <ApplicationServices/ApplicationServices.h>

#include <cstdint>

#include <spacehound/cf.hpp>

extern "C" {
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
