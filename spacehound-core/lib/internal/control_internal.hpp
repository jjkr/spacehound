#pragma once

#include <ApplicationServices/ApplicationServices.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <spacehound/cg.hpp>
#include <spacehound/control.hpp>
#include <spacehound/gesture.hpp>

namespace spacehound::control::detail {

struct workspace_bounds final {
  std::int64_t current_index = 0;
  std::int64_t num_spaces = 0;

  [[nodiscard]] auto operator==(const workspace_bounds &) const -> bool = default;
};

struct workspace_motion final {
  bool should_execute = false;
  gesture::direction direction = gesture::direction::right;
  std::size_t repeat_count = 0;

  [[nodiscard]] auto operator==(const workspace_motion &) const -> bool = default;
};

struct display_record final {
  CGDirectDisplayID display_id = 0;
  std::string uuid;
  CGRect bounds{};
};

struct window_record final {
  CGWindowID window_id = 0;
  pid_t pid = 0;
  std::string owner_name;
  std::string title;
  std::int64_t layer = 0;
  CGRect bounds{};
  bool is_onscreen = false;
};

struct display_switch_plan final {
  bool should_execute = false;
  std::size_t target_index = 0;

  [[nodiscard]] auto operator==(const display_switch_plan &) const -> bool = default;
};

struct window_cycle_state final {
  std::vector<CGWindowID> window_order;
  std::size_t current_index = 0;
  std::uint64_t last_cycle_timestamp_ms = 0;

  [[nodiscard]] auto operator==(const window_cycle_state &) const -> bool = default;
};

struct window_focus_plan final {
  std::optional<CGWindowID> target_window_id;
  std::optional<window_cycle_state> next_state;

  [[nodiscard]] auto operator==(const window_focus_plan &) const -> bool = default;
};

[[nodiscard]] auto action_name(const control::request &request) noexcept -> std::string_view;

[[nodiscard]] auto is_unified_spaces_display_identifier(
    std::string_view display_identifier) noexcept -> bool;

[[nodiscard]] auto plan_workspace_request(
    const control::workspace_request &request,
    std::int64_t current_index,
    std::int64_t num_spaces) noexcept -> workspace_motion;

void sort_displays_left_to_right(std::vector<display_record> &displays) noexcept;

// The active menu-bar display, as the window server reports it, except that
// for a short while after a display switch it answers the switch target: the
// reported value lags the focus change by 50-100ms.
[[nodiscard]] auto active_display_identifier() -> std::optional<std::string>;

[[nodiscard]] auto find_current_display_index(
    std::span<const display_record> displays,
    std::string_view active_display_uuid) noexcept -> std::optional<std::size_t>;

[[nodiscard]] auto find_display_index_containing_point(
    std::span<const display_record> displays,
    CGPoint point) noexcept -> std::optional<std::size_t>;

[[nodiscard]] auto load_active_displays(std::vector<display_record> &out_displays) -> bool;

[[nodiscard]] auto plan_display_request(
    const control::display_request &request,
    std::size_t current_index,
    std::size_t display_count) noexcept -> display_switch_plan;

[[nodiscard]] auto cursor_anchor_point(CGRect display_bounds) noexcept -> CGPoint;

// Points on `target_bounds`' menu bar to try clicking, most likely empty
// first: the middle, then alternating outward. Menu titles sit on the left and
// status items on the right, so the gap is normally around the middle.
[[nodiscard]] auto menu_bar_click_candidates(CGRect target_bounds) -> std::vector<CGPoint>;

[[nodiscard]] auto ensure_cursor_on_display(
    cg::event_source_view synthetic_source,
    CGRect display_bounds) noexcept -> bool;

[[nodiscard]] auto find_frontmost_window_index_on_display(
    std::span<const window_record> windows,
    CGRect target_bounds) noexcept -> std::optional<std::size_t>;

[[nodiscard]] auto collect_window_ids_on_display(
    std::span<const window_record> windows,
    CGRect target_bounds) -> std::vector<CGWindowID>;

[[nodiscard]] auto plan_window_focus_request(
    const control::window_focus_request &request,
    std::span<const CGWindowID> current_window_ids,
    const std::optional<window_cycle_state> &previous_state,
    std::uint64_t now_ms,
    std::uint64_t timeout_ms) -> window_focus_plan;

[[nodiscard]] auto execute_display_request(
    const control::display_request &request,
    cg::event_source_view synthetic_source) -> std::expected<void, control::error>;

[[nodiscard]] auto execute_window_focus_request(
    const control::window_focus_request &request) -> std::expected<void, control::error>;

[[nodiscard]] auto execute_request(
    const control::request &request,
    cg::event_source_view synthetic_source) -> std::expected<void, control::error>;

}  // namespace spacehound::control::detail
