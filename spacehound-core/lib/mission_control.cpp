// Window cycling while Mission Control or App Exposé is showing: instead of
// focusing windows, move the overlay's hover highlight between thumbnails.
//
// Findings the code below relies on (macOS 26, see examples/mc_probe.cpp):
// - The Dock's "mc" / "appexpose" groups are stubs with no children; the real
//   tree belongs to the WindowManager process:
//     AXApplication "WindowManager"
//       AXGroup id="mc.display" | "appexpose.display"  (AXFrame = display
//                bounds, AXDisplayID = CGDirectDisplayID; one per display)
//         AXButton id="<bundle>.space.<ManagedSpaceID>" (Mission Control) or
//                  id="<bundle>" (App Exposé): title, AXFrame, "wid" = window id
//         AXGroup id="mc.spaces" ... (the spaces bar; its buttons have no "wid")
// - In Mission Control each display group lists thumbnails of every space on
//   that display; only those on the display's current space are visible.
// - The highlight follows the real cursor: a mouse-moved posted to the
//   WindowManager pid does nothing, one posted through the HID tap works, and
//   the highlight stays after the cursor is warped away again.

#include "internal/control_internal.hpp"
#include "internal/daemon_internal.hpp"
#include "internal/logging.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spacehound/ax.hpp>
#include <spacehound/cf.hpp>
#include <spacehound/cg.hpp>
#include <spacehound/cgs.hpp>
#include <spacehound/dispatch.hpp>

namespace spacehound::control::detail {
namespace {

namespace ax = spacehound::ax;
namespace cf = spacehound::cf;
namespace cg = spacehound::cg;
namespace cgs = spacehound::cgs;
namespace diagnostics = spacehound::diagnostics;
namespace dispatch = spacehound::dispatch;

// A hover session outlives the focus-cycle one: the highlight stays until
// the mouse moves, which the state tracks, so the timeout is only a backstop
// against a stale session after the overlay was closed and reopened.
constexpr std::uint64_t thumbnail_cycle_timeout_ms = 60000;
constexpr auto cursor_still_tolerance = 1.0;
// The overlay's appear animation: poll this often, give up after this long.
constexpr long long overlay_poll_interval_ms = 40;
constexpr long long overlay_poll_timeout_ms = 1500;
constexpr std::size_t max_tree_depth = 4;
constexpr auto display_frame_tolerance = 1.0;
constexpr auto thumbnail_space_marker = std::string_view{".space."};

inline const auto display_identifier_key = cf::string_view{CFSTR("Display Identifier")};
inline const auto current_space_key = cf::string_view{CFSTR("Current Space")};
inline const auto managed_space_id_key = cf::string_view{CFSTR("ManagedSpaceID")};
inline const auto display_id_attribute = cf::string_view{CFSTR("AXDisplayID")};
inline const auto window_id_attribute = cf::string_view{CFSTR("wid")};
constexpr std::string_view display_group_identifiers[] = {"mc.display", "appexpose.display"};

std::mutex thumbnail_cycle_state_mutex;
std::optional<thumbnail_cycle_state> thumbnail_cycle_state_cache;

auto runtime_error(std::string message) -> control::error {
  return control::error{
      .code = control::error_code::runtime_error,
      .message = std::move(message),
  };
}

auto state_error(std::string message) -> control::error {
  return control::error{
      .code = control::error_code::state_unavailable,
      .message = std::move(message),
  };
}

auto rect_center(CGRect rect) noexcept -> CGPoint {
  return CGPointMake(
      rect.origin.x + rect.size.width / 2.0, rect.origin.y + rect.size.height / 2.0);
}

auto rect_contains_point(CGRect rect, CGPoint point) noexcept -> bool {
  return point.x >= rect.origin.x && point.x < rect.origin.x + rect.size.width &&
         point.y >= rect.origin.y && point.y < rect.origin.y + rect.size.height;
}

auto rects_match(CGRect lhs, CGRect rhs) noexcept -> bool {
  return std::fabs(lhs.origin.x - rhs.origin.x) <= display_frame_tolerance &&
         std::fabs(lhs.origin.y - rhs.origin.y) <= display_frame_tolerance &&
         std::fabs(lhs.size.width - rhs.size.width) <= display_frame_tolerance &&
         std::fabs(lhs.size.height - rhs.size.height) <= display_frame_tolerance;
}

auto ax_string(ax::ui_element_view element, cf::string_view attribute) -> std::optional<std::string> {
  cf::type value;
  if (element.copy_attribute_value(attribute, value) != kAXErrorSuccess || !value) {
    return std::nullopt;
  }

  const auto string = value.cast<CFStringRef>();
  if (!string) {
    return std::nullopt;
  }

  return cf::string_view{string}.to_utf8();
}

auto ax_int64(ax::ui_element_view element, cf::string_view attribute) -> std::optional<std::int64_t> {
  cf::type value;
  if (element.copy_attribute_value(attribute, value) != kAXErrorSuccess || !value) {
    return std::nullopt;
  }

  const auto number = value.cast<CFNumberRef>();
  if (!number) {
    return std::nullopt;
  }

  std::int64_t result = 0;
  if (CFNumberGetValue(number.get(), kCFNumberSInt64Type, &result) == 0) {
    return std::nullopt;
  }

  return result;
}

auto ax_frame(ax::ui_element_view element) -> std::optional<CGRect> {
  cf::type value;
  if (element.copy_attribute_value(ax::frame_attribute, value) != kAXErrorSuccess || !value) {
    return std::nullopt;
  }

  const auto ax_value = value.cast<AXValueRef>();
  CGRect frame{};
  if (!ax_value || !ax::value_view{ax_value.get()}.get_value(frame)) {
    return std::nullopt;
  }

  return frame;
}

auto ax_children(ax::ui_element_view element) -> cf::type {
  cf::type value;
  if (element.copy_attribute_value(ax::children_attribute, value) != kAXErrorSuccess) {
    return {};
  }

  return value;
}

// Calls `visit(child)` for each AX child of `element`; stops early when
// `visit` returns false.
template <typename Visit>
void for_each_child(ax::ui_element_view element, Visit &&visit) {
  const auto value = ax_children(element);
  const auto children = value ? value.cast<CFArrayRef>() : cf::view<CFArrayRef>{};
  if (!children) {
    return;
  }

  const auto count = CFArrayGetCount(children.get());
  for (CFIndex index = 0; index < count; ++index) {
    const auto child = cf::array_at<AXUIElementRef>(children, index);
    if (child && !visit(ax::ui_element_view{child})) {
      return;
    }
  }
}

// The display group for `display`: matched by AXDisplayID, or by frame when
// the attribute is missing. App Exposé only has a group on the display it is
// shown on, so when nothing matches and there is a single group, that is it.
auto find_display_group(ax::ui_element_view window_manager, const display_record &display)
    -> ax::ui_element {
  ax::ui_element found;
  ax::ui_element only_group;
  std::size_t group_count = 0;
  for_each_child(window_manager, [&](ax::ui_element_view child) {
    const auto identifier = ax_string(child, ax::identifier_attribute);
    if (!identifier || std::ranges::find(display_group_identifiers, *identifier) ==
                           std::end(display_group_identifiers)) {
      return true;
    }

    ++group_count;
    only_group = ax::ui_element::retain(child);

    const auto display_id = ax_int64(child, display_id_attribute);
    const bool matches = display_id
                             ? *display_id == static_cast<std::int64_t>(display.display_id)
                             : ax_frame(child).transform([&](CGRect frame) {
                                 return rects_match(frame, display.bounds);
                               }).value_or(false);
    if (!matches) {
      return true;
    }

    found = ax::ui_element::retain(child);
    return false;
  });

  if (!found && group_count == 1) {
    return only_group;
  }

  return found;
}

// Collects the window thumbnails below `element`: the elements carrying a
// "wid". The walk is depth-limited because the spaces bar nests a few levels.
void collect_thumbnails(
    ax::ui_element_view element,
    std::size_t depth,
    std::vector<thumbnail_record> &out_thumbnails) {
  if (depth > max_tree_depth) {
    return;
  }

  for_each_child(element, [&](ax::ui_element_view child) {
    const auto window_id = ax_int64(child, window_id_attribute);
    if (!window_id) {
      collect_thumbnails(child, depth + 1, out_thumbnails);
      return true;
    }

    const auto frame = ax_frame(child);
    if (!frame || frame->size.width <= 0.0 || frame->size.height <= 0.0) {
      return true;
    }

    const auto identifier = ax_string(child, ax::identifier_attribute);
    out_thumbnails.push_back(thumbnail_record{
        .window_id = static_cast<CGWindowID>(*window_id),
        .space_id = identifier ? parse_thumbnail_space_id(*identifier).value_or(-1) : -1,
        .frame = *frame,
    });
    return true;
  });
}

auto dictionary_int64(cf::dictionary_view dictionary, cf::string_view key)
    -> std::optional<std::int64_t> {
  const auto number = dictionary.find<CFNumberRef>(key);
  std::int64_t value = 0;
  if (!number || CFNumberGetValue(number.get(), kCFNumberSInt64Type, &value) == 0) {
    return std::nullopt;
  }

  return value;
}

// Calls `visit(pid, layer, bounds)` for each window owned by WindowManager in
// the window list selected by `option`; stops when `visit` returns false.
template <typename Visit>
void for_each_window_manager_window(CGWindowListOption option, Visit &&visit) {
  const auto windows = cg::copy_window_info(option, kCGNullWindowID);
  if (!windows) {
    return;
  }

  const auto windows_view = cf::view<CFArrayRef>{windows.get()};
  const auto count = CFArrayGetCount(windows_view.get());
  for (CFIndex index = 0; index < count; ++index) {
    const auto window_dict_ref = cf::array_at<CFDictionaryRef>(windows_view, index);
    if (!window_dict_ref) {
      continue;
    }

    const auto window_dict = cf::dictionary_view{window_dict_ref};
    const auto owner_name = window_dict.find<CFStringRef>(cg::window_owner_name_key);
    if (!owner_name || cf::string_view{owner_name}.to_utf8() != "WindowManager") {
      continue;
    }

    const auto pid = dictionary_int64(window_dict, cg::window_owner_pid_key);
    if (!pid || *pid <= 0) {
      continue;
    }

    CGRect bounds{};
    if (const auto bounds_dict = window_dict.find<CFDictionaryRef>(cg::window_bounds_key)) {
      (void)CGRectMakeWithDictionaryRepresentation(bounds_dict.get(), &bounds);
    }

    if (!visit(static_cast<pid_t>(*pid),
               dictionary_int64(window_dict, cg::window_layer_key).value_or(0),
               bounds)) {
      return;
    }
  }
}

auto now_ms() -> std::uint64_t {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

}  // namespace

auto parse_thumbnail_space_id(std::string_view identifier) noexcept
    -> std::optional<std::int64_t> {
  const auto marker = identifier.rfind(thumbnail_space_marker);
  if (marker == std::string_view::npos) {
    return std::nullopt;
  }

  const auto digits = identifier.substr(marker + thumbnail_space_marker.size());
  std::int64_t space_id = 0;
  const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), space_id);
  if (error != std::errc{} || end != digits.data() + digits.size() || digits.empty()) {
    return std::nullopt;
  }

  return space_id;
}

auto visible_thumbnails_in_reading_order(
    std::span<const thumbnail_record> thumbnails,
    std::int64_t space_id) -> std::vector<thumbnail_record> {
  std::vector<thumbnail_record> visible;
  for (const auto &thumbnail : thumbnails) {
    if (thumbnail.space_id < 0 || thumbnail.space_id == space_id) {
      visible.push_back(thumbnail);
    }
  }

  if (visible.empty()) {
    return visible;
  }

  std::vector<double> heights;
  heights.reserve(visible.size());
  for (const auto &thumbnail : visible) {
    heights.push_back(thumbnail.frame.size.height);
  }
  std::ranges::nth_element(heights, heights.begin() + static_cast<std::ptrdiff_t>(heights.size() / 2));
  const auto row_tolerance = heights[heights.size() / 2] / 2.0;

  std::ranges::stable_sort(visible, [](const thumbnail_record &lhs, const thumbnail_record &rhs) {
    return rect_center(lhs.frame).y < rect_center(rhs.frame).y;
  });

  // Band consecutive thumbnails into rows, then order each row by x.
  auto row_begin = visible.begin();
  while (row_begin != visible.end()) {
    const auto row_y = rect_center(row_begin->frame).y;
    auto row_end = row_begin + 1;
    while (row_end != visible.end() && rect_center(row_end->frame).y - row_y <= row_tolerance) {
      ++row_end;
    }

    std::stable_sort(row_begin, row_end, [](const thumbnail_record &lhs, const thumbnail_record &rhs) {
      return rect_center(lhs.frame).x < rect_center(rhs.frame).x;
    });
    row_begin = row_end;
  }

  return visible;
}

auto find_thumbnail_index_containing_point(
    std::span<const thumbnail_record> thumbnails,
    CGPoint point) noexcept -> std::optional<std::size_t> {
  for (std::size_t index = 0; index < thumbnails.size(); ++index) {
    if (rect_contains_point(thumbnails[index].frame, point)) {
      return index;
    }
  }

  return std::nullopt;
}

auto plan_thumbnail_cycle(
    const control::window_focus_request &request,
    std::span<const thumbnail_record> thumbnails,
    const std::optional<thumbnail_cycle_state> &previous_state,
    std::optional<CGPoint> cursor,
    std::optional<CGWindowID> frontmost_window,
    std::uint64_t now_ms,
    std::uint64_t timeout_ms) -> thumbnail_cycle_plan {
  if (thumbnails.empty()) {
    return {};
  }

  const auto count = thumbnails.size();
  const bool forward = request.direction == control::window_focus_direction::next;
  const auto step = [&](std::size_t index) {
    return forward ? (index + 1U) % count : (index + count - 1U) % count;
  };
  const auto index_of_window = [&](CGWindowID window_id) -> std::optional<std::size_t> {
    for (std::size_t index = 0; index < count; ++index) {
      if (thumbnails[index].window_id == window_id) {
        return index;
      }
    }
    return std::nullopt;
  };

  const bool cursor_still = previous_state && cursor &&
                            std::fabs(cursor->x - previous_state->cursor_x) <= cursor_still_tolerance &&
                            std::fabs(cursor->y - previous_state->cursor_y) <= cursor_still_tolerance;

  std::optional<std::size_t> current_index;
  if (previous_state && cursor_still && now_ms >= previous_state->last_cycle_timestamp_ms &&
      now_ms - previous_state->last_cycle_timestamp_ms < timeout_ms &&
      previous_state->current_index < previous_state->window_order.size()) {
    current_index = index_of_window(previous_state->window_order[previous_state->current_index]);
  }

  if (!current_index && cursor) {
    current_index = find_thumbnail_index_containing_point(thumbnails, *cursor);
  }

  std::optional<std::size_t> target_index;
  if (current_index) {
    target_index = step(*current_index);
  } else if (frontmost_window) {
    target_index = index_of_window(*frontmost_window);
  }
  if (!target_index) {
    target_index = forward ? 0U : count - 1U;
  }

  const auto target_center = rect_center(thumbnails[*target_index].frame);
  thumbnail_cycle_state next_state{
      .current_index = *target_index,
      .last_cycle_timestamp_ms = now_ms,
      .cursor_x = cursor ? cursor->x : 0.0,
      .cursor_y = cursor ? cursor->y : 0.0,
      .target_x = target_center.x,
      .target_y = target_center.y,
  };
  next_state.window_order.reserve(count);
  for (const auto &thumbnail : thumbnails) {
    next_state.window_order.push_back(thumbnail.window_id);
  }

  return thumbnail_cycle_plan{
      .target_index = *target_index,
      .next_state = std::move(next_state),
  };
}

auto window_manager_pid() -> std::optional<pid_t> {
  std::optional<pid_t> found;
  for_each_window_manager_window(
      static_cast<CGWindowListOption>(kCGWindowListOptionAll),
      [&](pid_t pid, std::int64_t, CGRect) {
        found = pid;
        return false;
      });
  return found;
}

auto overlay_is_showing() -> bool {
  std::vector<display_record> displays;
  if (!load_active_displays(displays)) {
    return false;
  }

  bool showing = false;
  for_each_window_manager_window(
      static_cast<CGWindowListOption>(kCGWindowListOptionOnScreenOnly),
      [&](pid_t, std::int64_t layer, CGRect bounds) {
        if (layer <= 0) {
          return true;
        }
        for (const auto &display : displays) {
          if (rects_match(bounds, display.bounds)) {
            showing = true;
            return false;
          }
        }
        return true;
      });
  return showing;
}

auto current_space_id_for_display(std::string_view display_uuid) -> std::optional<std::int64_t> {
  const auto managed_spaces = cgs::copy_managed_display_spaces(cgs::main_connection_id());
  if (!managed_spaces) {
    return std::nullopt;
  }

  const auto displays = cf::view<CFArrayRef>{managed_spaces.get()};
  const auto count = CFArrayGetCount(displays.get());
  for (CFIndex index = 0; index < count; ++index) {
    const auto display_dict_ref = cf::array_at<CFDictionaryRef>(displays, index);
    if (!display_dict_ref) {
      continue;
    }

    const auto display_dict = cf::dictionary_view{display_dict_ref};
    const auto identifier = display_dict.find<CFStringRef>(display_identifier_key);
    const auto identifier_utf8 = identifier ? cf::string_view{identifier}.to_utf8() : std::nullopt;
    const bool matches = identifier_utf8 && (*identifier_utf8 == display_uuid ||
                                             is_unified_spaces_display_identifier(*identifier_utf8));
    if (!matches) {
      continue;
    }

    const auto current_space = display_dict.find<CFDictionaryRef>(current_space_key);
    if (!current_space) {
      return std::nullopt;
    }

    return dictionary_int64(cf::dictionary_view{current_space}, managed_space_id_key);
  }

  return std::nullopt;
}

auto load_thumbnails_for_display(
    pid_t window_manager_pid,
    const display_record &display,
    std::vector<thumbnail_record> &out_thumbnails) -> bool {
  out_thumbnails.clear();

  const auto window_manager = ax::ui_element::create_application(window_manager_pid);
  if (!window_manager) {
    return false;
  }

  const auto group = find_display_group(window_manager.view(), display);
  if (!group) {
    return false;
  }

  collect_thumbnails(group.view(), 0, out_thumbnails);
  return true;
}

auto hover_thumbnail(cg::event_source_view synthetic_source, CGPoint point) -> bool {
  auto event =
      cg::event::create_mouse(synthetic_source, kCGEventMouseMoved, point, kCGMouseButtonLeft);
  if (!event) {
    return false;
  }
  event.set_flags(0);

  return cursor_hop(synthetic_source, point, kCGEventMouseMoved, true, [&] {
    event.post(kCGHIDEventTap);
  });
}

namespace {

// Everything a hover needs about the active display's overlay.
struct overlay_snapshot final {
  pid_t window_manager = 0;
  std::vector<thumbnail_record> thumbnails;  // visible, reading order
  std::optional<CGWindowID> frontmost_window;
};

auto load_overlay_snapshot(const display_record &display, overlay_snapshot &out)
    -> std::expected<void, control::error> {
  const auto pid = window_manager_pid();
  if (!pid) {
    return std::unexpected(state_error("Failed to find the WindowManager process."));
  }

  const auto space_id = current_space_id_for_display(display.uuid);
  if (!space_id) {
    return std::unexpected(state_error("Failed to determine the active display's current space."));
  }

  std::vector<thumbnail_record> all_thumbnails;
  if (!load_thumbnails_for_display(*pid, display, all_thumbnails)) {
    return std::unexpected(state_error("Failed to read the Mission Control thumbnails."));
  }

  out.window_manager = *pid;
  out.thumbnails = visible_thumbnails_in_reading_order(std::span{all_thumbnails}, *space_id);
  out.frontmost_window.reset();
  if (std::vector<window_record> windows; load_on_screen_windows(windows)) {
    if (const auto index = find_frontmost_window_index_on_display(std::span{windows}, display.bounds)) {
      out.frontmost_window = windows[*index].window_id;
    }
  }

  return {};
}

// Hovers `thumbnails[index]` and records the session so the next cycle
// hotkey continues from it.
auto hover_and_remember(
    cg::event_source_view synthetic_source,
    std::span<const thumbnail_record> thumbnails,
    std::size_t index,
    std::uint64_t timestamp_ms) -> bool {
  const auto target_center = rect_center(thumbnails[index].frame);
  if (!hover_thumbnail(synthetic_source, target_center)) {
    return false;
  }

  thumbnail_cycle_state state{
      .current_index = index,
      .last_cycle_timestamp_ms = timestamp_ms,
      .target_x = target_center.x,
      .target_y = target_center.y,
  };
  state.window_order.reserve(thumbnails.size());
  for (const auto &thumbnail : thumbnails) {
    state.window_order.push_back(thumbnail.window_id);
  }
  // The hop rounds the cursor to whole points; remember where it ended up.
  if (const auto cursor_event = cg::event::create(synthetic_source)) {
    const auto cursor = cursor_event.location();
    state.cursor_x = cursor.x;
    state.cursor_y = cursor.y;
  }

  std::lock_guard lock(thumbnail_cycle_state_mutex);
  thumbnail_cycle_state_cache = std::move(state);
  return true;
}

// One polling job for `highlight_frontmost_window_when_overlay_appears`.
struct overlay_highlight_job final {
  std::chrono::steady_clock::time_point deadline;
  std::optional<display_record> display;
  std::optional<CGRect> last_frame;  // the target's frame on the previous tick
};

void poll_overlay_highlight(void *raw_job) {
  std::unique_ptr<overlay_highlight_job> job{static_cast<overlay_highlight_job *>(raw_job)};

  const auto reschedule = [&] {
    if (std::chrono::steady_clock::now() >= job->deadline) {
      os_log_debug(diagnostics::navigation_log(),
                   "Overlay highlight gave up waiting for the thumbnails");
      return;
    }
    dispatch::to_main_after_ms(overlay_poll_interval_ms, poll_overlay_highlight, job.release());
  };

  if (!job->display) {
    std::vector<display_record> displays;
    const auto active_uuid = active_display_identifier();
    if (load_active_displays(displays) && active_uuid) {
      if (const auto index = find_current_display_index(displays, *active_uuid)) {
        job->display = displays[*index];
      }
    }
    if (!job->display) {
      reschedule();
      return;
    }
  }

  overlay_snapshot snapshot;
  if (!load_overlay_snapshot(*job->display, snapshot) || snapshot.thumbnails.empty() ||
      !snapshot.frontmost_window) {
    reschedule();
    return;
  }

  std::optional<std::size_t> target;
  for (std::size_t index = 0; index < snapshot.thumbnails.size(); ++index) {
    if (snapshot.thumbnails[index].window_id == *snapshot.frontmost_window) {
      target = index;
      break;
    }
  }
  if (!target) {
    reschedule();
    return;
  }

  // Wait for the appear animation: hover once the frame stops changing.
  const auto frame = snapshot.thumbnails[*target].frame;
  if (!job->last_frame || !CGRectEqualToRect(*job->last_frame, frame)) {
    job->last_frame = frame;
    reschedule();
    return;
  }

  auto source = cg::event_source::create(kCGEventSourceStateHIDSystemState);
  if (!source) {
    return;
  }
  source.set_user_data(spacehound::daemon::detail::synthetic_event_marker);

  if (hover_and_remember(source.view(), std::span{snapshot.thumbnails}, *target, now_ms())) {
    os_log_info(diagnostics::navigation_log(),
                "Overlay highlight hovered the frontmost window (thumbnails=%{public}zu index=%{public}zu)",
                snapshot.thumbnails.size(),
                *target);
  } else {
    os_log_error(diagnostics::navigation_log(), "Overlay highlight failed to post the hover event");
  }
}

}  // namespace

void highlight_frontmost_window_when_overlay_appears() {
  auto *job = new overlay_highlight_job{
      .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{overlay_poll_timeout_ms},
  };
  dispatch::to_main_after_ms(overlay_poll_interval_ms, poll_overlay_highlight, job);
}

namespace {

constexpr long long dismissal_poll_interval_ms = 20;
constexpr long long dismissal_poll_timeout_ms = 1000;

// The cleanup after `prepare_overlay_dismissal` parked the cursor.
struct overlay_dismissal_job final {
  std::chrono::steady_clock::time_point deadline;
  CGPoint original_cursor{};
  CGWindowID window_id = 0;
  std::string display_uuid;
};

void finish_overlay_dismissal(void *raw_job) {
  std::unique_ptr<overlay_dismissal_job> job{static_cast<overlay_dismissal_job *>(raw_job)};

  if (overlay_is_showing() && std::chrono::steady_clock::now() < job->deadline) {
    dispatch::to_main_after_ms(dismissal_poll_interval_ms, finish_overlay_dismissal, job.release());
    return;
  }

  (void)cg::warp_mouse_cursor_position(job->original_cursor);
  (void)cg::show_cursor();

  // The overlay normally activated the window itself; make sure.
  std::vector<window_record> windows;
  if (!load_on_screen_windows(windows)) {
    return;
  }
  std::vector<display_record> displays;
  const auto display = load_active_displays(displays)
                           ? std::ranges::find(displays, job->display_uuid, &display_record::uuid)
                           : displays.end();
  if (display == displays.end()) {
    return;
  }
  const auto frontmost = find_frontmost_window_index_on_display(std::span{windows}, display->bounds);
  if (frontmost && windows[*frontmost].window_id == job->window_id) {
    os_log_info(diagnostics::navigation_log(), "Overlay dismissal activated the highlighted window");
    return;
  }

  const auto window = std::ranges::find(windows, job->window_id, &window_record::window_id);
  if (window == windows.end()) {
    os_log_debug(diagnostics::navigation_log(), "Overlay dismissal lost the highlighted window");
    return;
  }
  const bool focused = focus_window(*window, job->display_uuid);
  os_log_info(diagnostics::navigation_log(),
              "Overlay dismissal %{public}s the highlighted window itself",
              focused ? "focused" : "failed to focus");
}

}  // namespace

void prepare_overlay_dismissal(cg::event_source_view synthetic_source) {
  std::optional<thumbnail_cycle_state> state;
  {
    std::lock_guard lock(thumbnail_cycle_state_mutex);
    state = thumbnail_cycle_state_cache;
    thumbnail_cycle_state_cache.reset();
  }

  const auto cursor_event = cg::event::create(synthetic_source);
  if (!state || !cursor_event) {
    return;
  }
  const auto cursor = cursor_event.location();

  const bool cursor_still = std::fabs(cursor.x - state->cursor_x) <= cursor_still_tolerance &&
                            std::fabs(cursor.y - state->cursor_y) <= cursor_still_tolerance;
  if (!cursor_still || state->current_index >= state->window_order.size() ||
      now_ms() - state->last_cycle_timestamp_ms >= thumbnail_cycle_timeout_ms) {
    os_log_debug(diagnostics::navigation_log(),
                 "Overlay dismissal leaves the selection to the cursor");
    return;
  }

  const auto active_uuid = active_display_identifier();
  if (!active_uuid) {
    return;
  }

  const bool hidden = cgs::set_cursor_in_background(true) == kCGErrorSuccess &&
                      cg::hide_cursor() == kCGErrorSuccess;
  if (cg::warp_mouse_cursor_position(CGPointMake(state->target_x, state->target_y)) !=
      kCGErrorSuccess) {
    if (hidden) {
      (void)cg::show_cursor();
    }
    return;
  }

  auto *job = new overlay_dismissal_job{
      .deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{dismissal_poll_timeout_ms},
      .original_cursor = cursor,
      .window_id = state->window_order[state->current_index],
      .display_uuid = *active_uuid,
  };
  dispatch::to_main_after_ms(dismissal_poll_interval_ms, finish_overlay_dismissal, job);
}

auto execute_thumbnail_cycle_request(
    const control::window_focus_request &request,
    cg::event_source_view synthetic_source,
    const display_record &active_display) -> std::expected<void, control::error> {
  const auto started_at = std::chrono::steady_clock::now();
  const auto elapsed_ms = [&] {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - started_at)
        .count();
  };

  overlay_snapshot snapshot;
  if (const auto loaded = load_overlay_snapshot(active_display, snapshot); !loaded) {
    return std::unexpected(loaded.error());
  }

  const auto enumerated_ms = elapsed_ms();
  if (snapshot.thumbnails.empty()) {
    os_log_info(diagnostics::navigation_log(),
                "Window cycle found no thumbnails on the active display (enumerate=%{public}lldms)",
                static_cast<long long>(enumerated_ms));
    return {};
  }

  std::optional<CGPoint> cursor;
  if (const auto cursor_event = cg::event::create(synthetic_source)) {
    cursor = cursor_event.location();
  }

  std::optional<thumbnail_cycle_state> previous_state;
  {
    std::lock_guard lock(thumbnail_cycle_state_mutex);
    previous_state = thumbnail_cycle_state_cache;
  }

  const auto timestamp_ms = now_ms();
  const auto plan = plan_thumbnail_cycle(
      request,
      std::span{snapshot.thumbnails},
      previous_state,
      cursor,
      snapshot.frontmost_window,
      timestamp_ms,
      thumbnail_cycle_timeout_ms);
  if (!plan.target_index) {
    return {};
  }

  if (!hover_and_remember(synthetic_source, std::span{snapshot.thumbnails}, *plan.target_index, timestamp_ms)) {
    return std::unexpected(runtime_error("Failed to post the thumbnail hover event."));
  }

  os_log_info(diagnostics::navigation_log(),
              "Window cycle hovered a thumbnail (thumbnails=%{public}zu index=%{public}zu "
              "enumerate=%{public}lldms total=%{public}lldms)",
              snapshot.thumbnails.size(),
              *plan.target_index,
              static_cast<long long>(enumerated_ms),
              static_cast<long long>(elapsed_ms()));
  return {};
}

}  // namespace spacehound::control::detail
