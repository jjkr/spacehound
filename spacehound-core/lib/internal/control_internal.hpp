#pragma once

#include <ApplicationServices/ApplicationServices.h>

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <spacehound/cg.hpp>
#include <spacehound/control.hpp>
#include <spacehound/gesture.hpp>

namespace spacehound::control::detail {

enum class dock_view_state {
  hidden,
  mission_control,
  expose,
};

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
  double alpha = 1.0;
};

// A window's thumbnail in Mission Control / App Exposé: while an overlay
// shows, the window list reports the window at its thumbnail's bounds.
struct thumbnail_record final {
  CGWindowID window_id = 0;
  CGRect frame{};  // the thumbnail's on-screen rectangle

  [[nodiscard]] auto operator==(const thumbnail_record &other) const noexcept -> bool {
    return window_id == other.window_id && CGRectEqualToRect(frame, other.frame);
  }
};

struct thumbnail_cycle_state final {
  std::string display_uuid;   // the display the session is on
  std::vector<CGWindowID> window_order;  // may be empty: the display has no thumbnails
  std::size_t current_index = 0;
  std::uint64_t last_cycle_timestamp_ms = 0;
  // Where the cursor was after the hover; it moving since means the user
  // took the highlight elsewhere.
  double cursor_x = 0.0;
  double cursor_y = 0.0;
  // The hovered thumbnail's center.
  double target_x = 0.0;
  double target_y = 0.0;

  [[nodiscard]] auto operator==(const thumbnail_cycle_state &) const -> bool = default;
};

struct thumbnail_cycle_plan final {
  std::optional<std::size_t> target_index;
  std::optional<thumbnail_cycle_state> next_state;

  [[nodiscard]] auto operator==(const thumbnail_cycle_plan &) const -> bool = default;
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

[[nodiscard]] auto dock_view_state_name(dock_view_state state) noexcept -> std::string_view;

// The Dock's process id, from the window list.
[[nodiscard]] auto dock_pid() -> std::optional<pid_t>;

// Whether the Dock is currently showing Mission Control or App Exposé,
// decided from the identifiers in its accessibility hierarchy.
[[nodiscard]] auto detect_dock_view_state(pid_t dock_pid) -> std::optional<dock_view_state>;

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

// Runs `post` with the cursor warped to `point`, then warps it straight back.
// `post` must post events to the HID tap; the last of them is of `applied_type`
// and the hop waits (up to 50ms) until the window server has applied it, so
// that it does not drag the cursor back to `point` after we return. With
// `hide_cursor` the cursor is invisible for the hop; otherwise it shows up at
// `point` for at most a frame.
[[nodiscard]] auto cursor_hop(
    cg::event_source_view synthetic_source,
    CGPoint point,
    CGEventType applied_type,
    bool hide_cursor,
    const std::function<void()> &post) -> bool;

[[nodiscard]] auto ensure_cursor_on_display(
    cg::event_source_view synthetic_source,
    CGRect display_bounds) noexcept -> bool;

// Every on-screen window (desktop elements excluded), front to back.
[[nodiscard]] auto load_on_screen_windows(std::vector<window_record> &out_windows) -> bool;

// Brings `target_window` to the front (window-server fast path, AX fallback).
[[nodiscard]] auto focus_window(const window_record &target_window, std::string_view display_uuid) -> bool;

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

// --- Mission Control thumbnails (mission_control.cpp) ---

// `thumbnails` in reading order: rows top to bottom (thumbnails whose
// centers lie within half the median height of each other share a row),
// left to right within a row.
[[nodiscard]] auto thumbnails_in_reading_order(
    std::span<const thumbnail_record> thumbnails) -> std::vector<thumbnail_record>;

// The thumbnails on a display while an overlay shows: the on-screen, visible,
// layer-0 app windows (as the window list reports them, at thumbnail bounds)
// whose centers lie on `display_bounds`, in reading order. WindowManager's
// own windows (the highlight frame among them) are not thumbnails.
// With `only_pid`, only that process's windows count (App Exposé shows one
// app; the others' windows stay in the list at their usual places).
[[nodiscard]] auto thumbnails_on_display(
    std::span<const window_record> windows,
    CGRect display_bounds,
    std::optional<pid_t> only_pid = std::nullopt) -> std::vector<thumbnail_record>;

// The process whose windows the overlay shows: the frontmost application in
// App Exposé, none (every app) in Mission Control. Reads the Dock's state.
[[nodiscard]] auto overlay_app_filter() -> std::optional<pid_t>;

[[nodiscard]] auto find_thumbnail_index_containing_point(
    std::span<const thumbnail_record> thumbnails,
    CGPoint point) noexcept -> std::optional<std::size_t>;

// Whether a cycle session still describes the highlight: the cursor has not
// moved since its hover (the highlight follows the real cursor) and
// `timeout_ms` has not elapsed.
[[nodiscard]] auto session_is_current(
    const std::optional<thumbnail_cycle_state> &state,
    std::optional<CGPoint> cursor,
    std::uint64_t now_ms,
    std::uint64_t timeout_ms) noexcept -> bool;

// The window `windows` (front to back) shows first among those with a
// thumbnail.
[[nodiscard]] auto frontmost_thumbnail_window(
    std::span<const window_record> windows,
    std::span<const thumbnail_record> thumbnails) noexcept -> std::optional<CGWindowID>;

// Which thumbnail to hover next. A session continues while it is current
// (`session_is_current`) and its last hovered window is still shown.
// Otherwise it starts by stepping away from the thumbnail under `cursor`;
// with no thumbnail there it lands on `frontmost_window`'s thumbnail, or
// failing that the first/last. The plan's state carries `display_uuid` and
// the current cursor, which the caller replaces with the post-hover position.
[[nodiscard]] auto plan_thumbnail_cycle(
    const control::window_focus_request &request,
    std::string_view display_uuid,
    std::span<const thumbnail_record> thumbnails,
    const std::optional<thumbnail_cycle_state> &previous_state,
    std::optional<CGPoint> cursor,
    std::optional<CGWindowID> frontmost_window,
    std::uint64_t now_ms,
    std::uint64_t timeout_ms) -> thumbnail_cycle_plan;

// The current managed space id of the display with Spaces identifier
// `display_uuid` (or of the single unified-spaces entry).
[[nodiscard]] auto current_space_id_for_display(std::string_view display_uuid)
    -> std::optional<std::int64_t>;

// The thumbnails currently shown on `display` (see `thumbnails_on_display`
// and `overlay_app_filter`).
[[nodiscard]] auto load_thumbnails_for_display(
    const display_record &display,
    std::optional<pid_t> only_pid,
    std::vector<thumbnail_record> &out_thumbnails) -> bool;

// Moves the Mission Control hover highlight to `point`: a mouse-moved posted
// through the HID tap during an invisible cursor hop (see `cursor_hop`).
[[nodiscard]] auto hover_thumbnail(cg::event_source_view synthetic_source, CGPoint point) -> bool;

// Highlights the frontmost window's thumbnail once Mission Control or App
// Exposé has appeared: polls WindowManager from the main queue (never
// blocking it) until the thumbnail's frame has settled or ~1.5s pass, hovers
// it, and seeds the cycle session so the next cycle hotkey steps on from it.
void highlight_frontmost_window_when_overlay_appears();

// The same, for `display` and only once its current space is
// `expected_space_id`: for a workspace switch inside Mission Control.
void highlight_frontmost_window_when_space_appears(
    display_record display,
    std::int64_t expected_space_id);

// Hovers `display`'s frontmost thumbnail right away (the overlay is already
// up) and moves the cycle session to that display; with no thumbnails there
// the session only remembers the display.
[[nodiscard]] auto highlight_frontmost_on_display(
    cg::event_source_view synthetic_source,
    const display_record &display) -> std::expected<void, control::error>;

// The display overlay-mode requests act on: the cycle session's while the
// session is current (hovering never moves the menu bar, so the window
// server still reports where the overlay was opened), else the active
// menu-bar display. An index into `displays`.
[[nodiscard]] auto overlay_display(
    cg::event_source_view synthetic_source,
    std::span<const display_record> displays) -> std::optional<std::size_t>;

// Whether Mission Control / App Exposé is on screen: WindowManager then owns
// a display-sized window above the normal layers. Cheap (no AX), so safe to
// call from the event tap.
[[nodiscard]] auto overlay_is_showing() -> bool;

// Call right before posting the swipe that dismisses the overlay. The
// overlay activates the thumbnail under the cursor as it closes, so when the
// cycle session's highlight is current (cursor unmoved since the hover) this
// parks the hidden cursor on that thumbnail, then schedules the cleanup on
// the main queue: once the overlay is gone the cursor returns and reappears,
// and the window is focused if the overlay did not do it. Otherwise the
// thumbnail under the cursor, if any, is what the overlay picks anyway. Ends
// the cycle session. No AX and no waiting, so safe from the event tap.
void prepare_overlay_dismissal(cg::event_source_view synthetic_source);

[[nodiscard]] auto execute_thumbnail_cycle_request(
    const control::window_focus_request &request,
    cg::event_source_view synthetic_source,
    const display_record &active_display) -> std::expected<void, control::error>;

[[nodiscard]] auto execute_display_request(
    const control::display_request &request,
    cg::event_source_view synthetic_source) -> std::expected<void, control::error>;

[[nodiscard]] auto execute_window_focus_request(
    const control::window_focus_request &request,
    cg::event_source_view synthetic_source) -> std::expected<void, control::error>;

[[nodiscard]] auto execute_request(
    const control::request &request,
    cg::event_source_view synthetic_source) -> std::expected<void, control::error>;

}  // namespace spacehound::control::detail
