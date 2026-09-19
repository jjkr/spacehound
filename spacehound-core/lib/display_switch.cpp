#include "internal/control_internal.hpp"
#include "internal/logging.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <spacehound/ax.hpp>
#include <spacehound/cf.hpp>
#include <spacehound/cg.hpp>
#include <spacehound/cgs.hpp>
#include <spacehound/ns.hpp>

namespace spacehound::control::detail {
namespace {

namespace ax = spacehound::ax;
namespace cf = spacehound::cf;
namespace cg = spacehound::cg;
namespace cgs = spacehound::cgs;
namespace ns = spacehound::ns;
namespace diagnostics = spacehound::diagnostics;

constexpr auto on_screen_exclude_desktop_option = static_cast<CGWindowListOption>(
    kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements);
constexpr auto ax_bounds_tolerance = 5.0;
constexpr std::uint64_t window_cycle_timeout_ms = 3000;

std::mutex window_cycle_state_mutex;
std::optional<window_cycle_state> window_cycle_state_cache;

// The window server reports the new active menu-bar display 50-100ms after a
// window there is focused. Remember the last switch so that requests arriving
// in that window plan from where we just went, not where we came from.
constexpr auto pending_display_switch_ttl = std::chrono::milliseconds{400};

struct pending_display_switch final {
  std::string from_uuid;
  std::string to_uuid;
  std::chrono::steady_clock::time_point at;
};

std::mutex pending_display_switch_mutex;
std::optional<pending_display_switch> pending_display_switch_cache;

void record_display_switch(std::string from_uuid, std::string to_uuid) {
  std::lock_guard lock(pending_display_switch_mutex);
  pending_display_switch_cache = pending_display_switch{
      .from_uuid = std::move(from_uuid),
      .to_uuid = std::move(to_uuid),
      .at = std::chrono::steady_clock::now(),
  };
}

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

auto rect_contains_point(CGRect rect, CGPoint point) noexcept -> bool {
  return point.x >= rect.origin.x && point.x < rect.origin.x + rect.size.width &&
         point.y >= rect.origin.y && point.y < rect.origin.y + rect.size.height;
}

auto window_center(const window_record &window) noexcept -> CGPoint {
  return CGPoint{
      .x = window.bounds.origin.x + window.bounds.size.width / 2.0,
      .y = window.bounds.origin.y + window.bounds.size.height / 2.0,
  };
}

auto dictionary_number_int64(
    cf::dictionary_view dictionary,
    cf::string_view key,
    std::int64_t &out_value) noexcept -> bool {
  const auto number = dictionary.find<CFNumberRef>(key);
  if (!number) {
    return false;
  }

  const auto value = cf::number_get<std::int64_t>(number);
  if (!value) {
    return false;
  }

  out_value = *value;
  return true;
}

auto dictionary_string_utf8(
    cf::dictionary_view dictionary,
    cf::string_view key) noexcept -> std::string {
  const auto value = dictionary.find<CFStringRef>(key);
  if (!value) {
    return {};
  }

  const auto utf8 = cf::string_view{value}.to_utf8();
  if (!utf8) {
    return {};
  }

  return *utf8;
}

auto dictionary_bool(
    cf::dictionary_view dictionary,
    cf::string_view key,
    bool default_value) noexcept -> bool {
  if (const auto boolean = dictionary.find<CFBooleanRef>(key)) {
    return CFBooleanGetValue(boolean.get()) != 0;
  }

  std::int64_t number_value = 0;
  if (dictionary_number_int64(dictionary, key, number_value)) {
    return number_value != 0;
  }

  return default_value;
}

auto dictionary_rect(
    cf::dictionary_view dictionary,
    cf::string_view key,
    CGRect &out_rect) noexcept -> bool {
  const auto bounds = dictionary.find<CFDictionaryRef>(key);
  if (!bounds) {
    return false;
  }

  return CGRectMakeWithDictionaryRepresentation(bounds.get(), &out_rect) != 0;
}

auto load_reported_active_display_uuid() -> std::optional<std::string> {
  const auto active_display =
      cgs::copy_active_menu_bar_display_identifier(cgs::main_connection_id());
  if (!active_display) {
    return std::nullopt;
  }

  return active_display.to_utf8();
}

auto load_active_display_uuid() -> std::optional<std::string> {
  return active_display_identifier();
}

auto load_on_screen_windows(std::vector<window_record> &out_windows) -> bool {
  const auto window_info = cg::copy_window_info(on_screen_exclude_desktop_option, kCGNullWindowID);
  if (!window_info) {
    return false;
  }

  out_windows.clear();
  const auto windows_view = cf::view<CFArrayRef>{window_info.get()};
  const auto count = CFArrayGetCount(windows_view.get());
  out_windows.reserve(static_cast<std::size_t>(count));

  for (CFIndex index = 0; index < count; ++index) {
    const auto window_dict_ref = cf::array_at<CFDictionaryRef>(windows_view, index);
    if (!window_dict_ref) {
      continue;
    }

    const auto window_dict = cf::dictionary_view{window_dict_ref};

    std::int64_t window_id = 0;
    std::int64_t pid = 0;
    std::int64_t layer = 0;
    CGRect bounds{};
    if (!dictionary_number_int64(window_dict, cg::window_number_key, window_id) ||
        !dictionary_number_int64(window_dict, cg::window_owner_pid_key, pid) ||
        !dictionary_number_int64(window_dict, cg::window_layer_key, layer) ||
        !dictionary_rect(window_dict, cg::window_bounds_key, bounds)) {
      continue;
    }

    out_windows.push_back(window_record{
        .window_id = static_cast<CGWindowID>(window_id),
        .pid = static_cast<pid_t>(pid),
        .owner_name = dictionary_string_utf8(window_dict, cg::window_owner_name_key),
        .title = dictionary_string_utf8(window_dict, cg::window_name_key),
        .layer = layer,
        .bounds = bounds,
        .is_onscreen = dictionary_bool(window_dict, cg::window_is_onscreen_key, true),
    });
  }

  return true;
}

auto ax_window_title(ax::ui_element_view window) -> std::string {
  cf::type value;
  if (window.copy_attribute_value(ax::title_attribute, value) != kAXErrorSuccess || !value) {
    return {};
  }

  if (const auto title = value.cast<CFStringRef>()) {
    const auto utf8 = cf::string_view{title}.to_utf8();
    if (utf8) {
      return *utf8;
    }
  }

  return {};
}

auto ax_window_bounds(ax::ui_element_view window, CGRect &out_bounds) -> bool {
  cf::type position_value;
  cf::type size_value;
  if (window.copy_attribute_value(ax::position_attribute, position_value) != kAXErrorSuccess ||
      window.copy_attribute_value(ax::size_attribute, size_value) != kAXErrorSuccess) {
    return false;
  }

  const auto position = position_value.cast<AXValueRef>();
  const auto size = size_value.cast<AXValueRef>();
  if (!position || !size) {
    return false;
  }

  CGPoint origin{};
  CGSize size_value_out{};
  if (!ax::value_view{position}.get_value(origin) ||
      !ax::value_view{size}.get_value(size_value_out)) {
    return false;
  }

  out_bounds = CGRect{
      .origin = origin,
      .size = size_value_out,
  };
  return true;
}

auto ax_window_matches_cg_window(
    ax::ui_element_view window,
    const window_record &target_window) -> bool {
  const auto title = ax_window_title(window);
  if (!title.empty() && !target_window.title.empty() && title == target_window.title) {
    return true;
  }

  CGRect bounds{};
  if (!ax_window_bounds(window, bounds)) {
    return false;
  }

  return std::fabs(bounds.origin.x - target_window.bounds.origin.x) <= ax_bounds_tolerance &&
         std::fabs(bounds.origin.y - target_window.bounds.origin.y) <= ax_bounds_tolerance &&
         std::fabs(bounds.size.width - target_window.bounds.size.width) <= ax_bounds_tolerance &&
         std::fabs(bounds.size.height - target_window.bounds.size.height) <= ax_bounds_tolerance;
}

auto find_target_ax_window(
    ax::ui_element_view app,
    const window_record &target_window) -> ax::ui_element {
  cf::type value;
  if (app.copy_attribute_value(ax::windows_attribute, value) != kAXErrorSuccess || !value) {
    return {};
  }

  const auto windows = value.cast<CFArrayRef>();
  if (!windows) {
    return {};
  }

  const auto count = CFArrayGetCount(windows.get());
  if (count <= 0) {
    return {};
  }

  if (count == 1) {
    const auto window_ref = cf::array_at<AXUIElementRef>(windows, 0);
    return window_ref ? ax::ui_element::retain(ax::ui_element_view{window_ref}) : ax::ui_element{};
  }

  for (CFIndex index = 0; index < count; ++index) {
    const auto window_ref = cf::array_at<AXUIElementRef>(windows, index);
    if (!window_ref) {
      continue;
    }

    CGWindowID window_id = 0;
    if (ax::ui_element_view{window_ref}.get_window(window_id) == kAXErrorSuccess &&
        window_id == target_window.window_id) {
      return ax::ui_element::retain(ax::ui_element_view{window_ref});
    }
  }

  for (CFIndex index = 0; index < count; ++index) {
    const auto window_ref = cf::array_at<AXUIElementRef>(windows, index);
    if (!window_ref) {
      continue;
    }

    const auto candidate = ax::ui_element_view{window_ref};
    if (ax_window_matches_cg_window(candidate, target_window)) {
      return ax::ui_element::retain(candidate);
    }
  }

  const auto fallback = cf::array_at<AXUIElementRef>(windows, 0);
  return fallback ? ax::ui_element::retain(ax::ui_element_view{fallback}) : ax::ui_element{};
}

void apply_window_focus(ax::ui_element_view app, ax::ui_element_view window) {
  (void)window.perform_action(ax::raise_action);
  (void)app.set_attribute_value(
      ax::focused_window_attribute, cf::type_view{reinterpret_cast<CFTypeRef>(window.get())});
  (void)window.set_attribute_value(ax::main_attribute, cf::type_view{kCFBooleanTrue});
}

// Returns once the window server reports `display_uuid` as the active
// menu-bar display, or after `timeout`. Polling NSRunningApplication.isActive
// instead would always run to the timeout: that property only refreshes when
// our run loop spins, which it does not while we wait here.
void wait_for_active_display(std::string_view display_uuid, std::chrono::milliseconds timeout) {
  using namespace std::chrono_literals;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (const auto active = load_reported_active_display_uuid();
        active && *active == display_uuid) {
      return;
    }

    std::this_thread::sleep_for(2ms);
  }
}

auto focus_window(const window_record &target_window, std::string_view display_uuid) -> bool {
  if (target_window.pid <= 0) {
    return false;
  }

  const auto app = ax::ui_element::create_application(target_window.pid);
  if (!app) {
    return false;
  }

  const auto window = find_target_ax_window(app.view(), target_window);
  if (!window) {
    return false;
  }

  using namespace std::chrono_literals;
  const auto running_application =
      ns::running_application::with_process_identifier(target_window.pid);
  const bool needs_activation = running_application && !running_application.is_active();

  // Fast path: tell the window server directly which window comes front (the
  // key-window records make it key in-process), then raise it. No need to
  // wait for the menu bar; `active_display_identifier` covers the lag.
  if (needs_activation && cgs::set_front_window(target_window.pid, target_window.window_id)) {
    (void)window.perform_action(ax::raise_action);
    return true;
  }

  apply_window_focus(app.view(), window.view());
  if (needs_activation && running_application.activate(ns::activate_ignoring_other_apps)) {
    wait_for_active_display(display_uuid, 100ms);
    apply_window_focus(app.view(), window.view());
  }

  return true;
}

auto find_window_index_by_id(
    std::span<const window_record> windows,
    CGWindowID window_id) noexcept -> std::optional<std::size_t> {
  for (std::size_t index = 0; index < windows.size(); ++index) {
    if (windows[index].window_id == window_id) {
      return index;
    }
  }

  return std::nullopt;
}

// The frontmost app's rightmost menu title, in global (top-left origin)
// coordinates on whichever display currently shows the active menu bar.
auto last_menu_title_frame() -> std::optional<CGRect> {
  const auto system_wide = ax::ui_element::create_system_wide();
  if (!system_wide) {
    return std::nullopt;
  }

  cf::type application_value;
  if (system_wide.copy_attribute_value(ax::focused_application_attribute, application_value) !=
          kAXErrorSuccess ||
      !application_value) {
    return std::nullopt;
  }
  const auto application = application_value.cast<AXUIElementRef>();
  if (!application) {
    return std::nullopt;
  }

  cf::type menu_bar_value;
  if (ax::ui_element_view{application}.copy_attribute_value(ax::menu_bar_attribute, menu_bar_value) !=
          kAXErrorSuccess ||
      !menu_bar_value) {
    return std::nullopt;
  }
  const auto menu_bar = menu_bar_value.cast<AXUIElementRef>();
  if (!menu_bar) {
    return std::nullopt;
  }

  cf::type children_value;
  if (ax::ui_element_view{menu_bar}.copy_attribute_value(ax::children_attribute, children_value) !=
          kAXErrorSuccess ||
      !children_value) {
    return std::nullopt;
  }
  const auto children = children_value.cast<CFArrayRef>();
  if (!children) {
    return std::nullopt;
  }

  const auto count = CFArrayGetCount(children.get());
  for (CFIndex index = count - 1; index >= 0; --index) {
    const auto item = cf::array_at<AXUIElementRef>(children, index);
    if (!item) {
      continue;
    }

    CGRect frame{};
    if (ax_window_bounds(ax::ui_element_view{item}, frame) && frame.size.width > 0.0) {
      return frame;
    }
  }

  return std::nullopt;
}

// Activates a display by clicking `point` on it. Posted mouse events move the
// cursor, and hiding it does not survive them, so the hop is kept below a
// frame instead: warp there synchronously, post the click, wait until the
// window server reports the mouse-up as applied, and warp straight back.
auto activate_display_with_click(cg::event_source_view synthetic_source, CGPoint point) -> bool {
  const auto cursor_event = cg::event::create(synthetic_source);
  if (!cursor_event) {
    return false;
  }
  const auto original = cursor_event.location();

  auto down_event =
      cg::event::create_mouse(synthetic_source, kCGEventLeftMouseDown, point, kCGMouseButtonLeft);
  auto up_event =
      cg::event::create_mouse(synthetic_source, kCGEventLeftMouseUp, point, kCGMouseButtonLeft);
  if (!down_event || !up_event) {
    return false;
  }
  down_event.set_flags(0);
  up_event.set_flags(0);

  const auto ups_before = cg::hid_event_count(kCGEventLeftMouseUp);
  if (cg::warp_mouse_cursor_position(point) != kCGErrorSuccess) {
    return false;
  }
  down_event.post(kCGHIDEventTap);
  up_event.post(kCGHIDEventTap);

  // Leave only once the up has been applied; otherwise it would drag the
  // cursor back to `point` after we return it.
  using namespace std::chrono_literals;
  const auto deadline = std::chrono::steady_clock::now() + 50ms;
  while (cg::hid_event_count(kCGEventLeftMouseUp) == ups_before &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }

  return cg::warp_mouse_cursor_position(original) == kCGErrorSuccess;
}

}  // namespace

void sort_displays_left_to_right(std::vector<display_record> &displays) noexcept {
  std::ranges::stable_sort(
      displays,
      [](const display_record &lhs, const display_record &rhs) {
        return lhs.bounds.origin.x < rhs.bounds.origin.x;
      });
}

auto active_display_identifier() -> std::optional<std::string> {
  auto reported = load_reported_active_display_uuid();

  std::lock_guard lock(pending_display_switch_mutex);
  if (!pending_display_switch_cache) {
    return reported;
  }

  const auto &pending = *pending_display_switch_cache;
  const bool expired = std::chrono::steady_clock::now() - pending.at > pending_display_switch_ttl;
  const bool caught_up = !reported || *reported != pending.from_uuid;
  if (expired || caught_up) {
    pending_display_switch_cache.reset();
    return reported;
  }

  return pending.to_uuid;
}

auto find_current_display_index(
    std::span<const display_record> displays,
    std::string_view active_display_uuid) noexcept -> std::optional<std::size_t> {
  for (std::size_t index = 0; index < displays.size(); ++index) {
    if (displays[index].uuid == active_display_uuid) {
      return index;
    }
  }

  return std::nullopt;
}

auto find_display_index_containing_point(
    std::span<const display_record> displays,
    CGPoint point) noexcept -> std::optional<std::size_t> {
  for (std::size_t index = 0; index < displays.size(); ++index) {
    if (rect_contains_point(displays[index].bounds, point)) {
      return index;
    }
  }

  return std::nullopt;
}

auto load_active_displays(std::vector<display_record> &out_displays) -> bool {
  std::vector<CGDirectDisplayID> display_ids;
  if (cg::active_displays(display_ids) != kCGErrorSuccess || display_ids.empty()) {
    return false;
  }

  out_displays.clear();
  out_displays.reserve(display_ids.size());
  for (const auto display_id : display_ids) {
    const auto uuid = cg::display_uuid_string(display_id);
    const auto utf8 = uuid.to_utf8();
    if (!utf8) {
      continue;
    }

    out_displays.push_back(display_record{
        .display_id = display_id,
        .uuid = *utf8,
        .bounds = cg::display_bounds(display_id),
    });
  }

  sort_displays_left_to_right(out_displays);
  return !out_displays.empty();
}


auto plan_display_request(
    const control::display_request &request,
    std::size_t current_index,
    std::size_t display_count) noexcept -> display_switch_plan {
  if (display_count == 0 || current_index >= display_count) {
    return {};
  }

  switch (request.action) {
    case control::display_action::left:
      if (current_index == 0) {
        if (!request.wrap) {
          return {};
        }

        return display_switch_plan{
            .should_execute = true,
            .target_index = display_count - 1,
        };
      }

      return display_switch_plan{
          .should_execute = true,
          .target_index = current_index - 1,
      };
    case control::display_action::right:
      if (current_index >= display_count - 1) {
        if (!request.wrap) {
          return {};
        }

        return display_switch_plan{
            .should_execute = true,
            .target_index = 0,
        };
      }

      return display_switch_plan{
          .should_execute = true,
          .target_index = current_index + 1,
      };
    case control::display_action::go_to:
      if (request.index == 0) {
        return {};
      }

      if (request.index > display_count || request.index - 1 == current_index) {
        return {};
      }

      return display_switch_plan{
          .should_execute = true,
          .target_index = request.index - 1,
      };
  }

  return {};
}

auto menu_bar_click_point(
    CGRect target_bounds,
    std::optional<CGRect> last_title_frame,
    CGRect title_display_bounds) noexcept -> CGPoint {
  constexpr auto margin = 12.0;
  const auto y = target_bounds.origin.y + 10.0;

  if (last_title_frame) {
    const auto title_max_x = last_title_frame->origin.x + last_title_frame->size.width;
    const auto x = target_bounds.origin.x + (title_max_x - title_display_bounds.origin.x) + margin;
    if (x <= target_bounds.origin.x + target_bounds.size.width - margin) {
      return CGPoint{.x = x, .y = y};
    }
  }

  return CGPoint{
      .x = target_bounds.origin.x + target_bounds.size.width / 2.0,
      .y = y,
  };
}

auto cursor_anchor_point(CGRect display_bounds) noexcept -> CGPoint {
  return CGPoint{
      .x = display_bounds.origin.x + display_bounds.size.width / 2.0,
      .y = display_bounds.origin.y + 1.0,
  };
}

auto ensure_cursor_on_display(
    cg::event_source_view synthetic_source,
    CGRect display_bounds) noexcept -> bool {
  const auto cursor_event = cg::event::create(synthetic_source);
  if (!cursor_event) {
    return false;
  }

  if (rect_contains_point(display_bounds, cursor_event.location())) {
    return true;
  }

  return cg::warp_mouse_cursor_position(cursor_anchor_point(display_bounds)) ==
         kCGErrorSuccess;
}

auto find_frontmost_window_index_on_display(
    std::span<const window_record> windows,
    CGRect target_bounds) noexcept -> std::optional<std::size_t> {
  for (std::size_t index = 0; index < windows.size(); ++index) {
    const auto &window = windows[index];
    if (!window.is_onscreen || window.layer != 0 || window.bounds.size.width <= 0.0 ||
        window.bounds.size.height <= 0.0) {
      continue;
    }

    if (rect_contains_point(target_bounds, window_center(window))) {
      return index;
    }
  }

  return std::nullopt;
}

auto collect_window_ids_on_display(
    std::span<const window_record> windows,
    CGRect target_bounds) -> std::vector<CGWindowID> {
  std::vector<CGWindowID> window_ids;
  window_ids.reserve(windows.size());

  for (const auto &window : windows) {
    if (!window.is_onscreen || window.layer != 0 || window.bounds.size.width <= 0.0 ||
        window.bounds.size.height <= 0.0) {
      continue;
    }

    if (rect_contains_point(target_bounds, window_center(window))) {
      window_ids.push_back(window.window_id);
    }
  }

  return window_ids;
}

auto plan_window_focus_request(
    const control::window_focus_request &request,
    std::span<const CGWindowID> current_window_ids,
    const std::optional<window_cycle_state> &previous_state,
    std::uint64_t now_ms,
    std::uint64_t timeout_ms) -> window_focus_plan {
  if (current_window_ids.size() <= 1U) {
    return {};
  }

  const auto can_continue = previous_state.has_value() &&
                            now_ms >= previous_state->last_cycle_timestamp_ms &&
                            now_ms - previous_state->last_cycle_timestamp_ms < timeout_ms &&
                            std::ranges::all_of(
                                previous_state->window_order,
                                [&](CGWindowID window_id) {
                                  return std::ranges::find(current_window_ids, window_id) !=
                                         current_window_ids.end();
                                });

  if (!can_continue) {
    const auto start_index = request.direction == control::window_focus_direction::next
                                 ? 1U
                                 : current_window_ids.size() - 1U;
    window_cycle_state next_state{
        .window_order = std::vector<CGWindowID>(current_window_ids.begin(), current_window_ids.end()),
        .current_index = start_index,
        .last_cycle_timestamp_ms = now_ms,
    };
    return window_focus_plan{
        .target_window_id = next_state.window_order[next_state.current_index],
        .next_state = std::move(next_state),
    };
  }

  window_cycle_state next_state = *previous_state;
  for (const auto window_id : current_window_ids) {
    if (std::ranges::find(next_state.window_order, window_id) == next_state.window_order.end()) {
      next_state.window_order.push_back(window_id);
    }
  }

  const auto window_count = next_state.window_order.size();
  if (request.direction == control::window_focus_direction::next) {
    next_state.current_index = (next_state.current_index + 1U) % window_count;
  } else if (next_state.current_index == 0U) {
    next_state.current_index = window_count - 1U;
  } else {
    next_state.current_index -= 1U;
  }
  next_state.last_cycle_timestamp_ms = now_ms;

  return window_focus_plan{
      .target_window_id = next_state.window_order[next_state.current_index],
      .next_state = std::move(next_state),
  };
}

auto execute_display_request(
    const control::display_request &request,
    cg::event_source_view synthetic_source) -> std::expected<void, control::error> {
  const auto started_at = std::chrono::steady_clock::now();
  const auto elapsed_ms = [&] {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - started_at)
        .count();
  };

  std::vector<display_record> displays;
  if (!load_active_displays(displays) || displays.size() <= 1U) {
    return {};
  }

  const auto active_display_uuid = load_active_display_uuid();
  if (!active_display_uuid) {
    return std::unexpected(state_error("Failed to determine the active display."));
  }

  const auto current_index = find_current_display_index(displays, *active_display_uuid);
  if (!current_index) {
    return std::unexpected(state_error("Failed to match the active display."));
  }

  const auto plan = plan_display_request(request, *current_index, displays.size());
  if (!plan.should_execute) {
    return {};
  }

  std::vector<window_record> windows;
  if (!load_on_screen_windows(windows)) {
    return std::unexpected(runtime_error("Failed to enumerate on-screen windows."));
  }

  const auto &target_display = displays[plan.target_index];
  if (request.move_cursor_to_target_display &&
      !ensure_cursor_on_display(synthetic_source, target_display.bounds)) {
    return std::unexpected(runtime_error("Failed to move the cursor to the target display."));
  }

  const auto target_window_index =
      find_frontmost_window_index_on_display(std::span{windows}, target_display.bounds);
  const auto prepared_ms = elapsed_ms();
  if (target_window_index) {
    if (focus_window(windows[*target_window_index], target_display.uuid)) {
      record_display_switch(*active_display_uuid, target_display.uuid);
      os_log_info(diagnostics::navigation_log(),
                  "Display switch focused a window (prepare=%{public}lldms total=%{public}lldms)",
                  static_cast<long long>(prepared_ms),
                  static_cast<long long>(elapsed_ms()));
      return {};
    }

    return std::unexpected(runtime_error("Failed to focus a window on the target display."));
  }

  // Nothing to focus: an empty display can only be activated by clicking it.
  // Aim for the empty menu-bar strip right of the app's last menu title.
  const auto title_frame = last_menu_title_frame();
  CGRect title_display_bounds = target_display.bounds;
  if (title_frame) {
    if (const auto index = find_display_index_containing_point(displays, title_frame->origin)) {
      title_display_bounds = displays[*index].bounds;
    }
  }

  const auto click_point =
      menu_bar_click_point(target_display.bounds, title_frame, title_display_bounds);
  if (!activate_display_with_click(synthetic_source, click_point)) {
    return std::unexpected(runtime_error("Failed to post a fallback menu-bar click."));
  }

  record_display_switch(*active_display_uuid, target_display.uuid);
  os_log_info(diagnostics::navigation_log(),
              "Display switch clicked an empty display (prepare=%{public}lldms total=%{public}lldms)",
              static_cast<long long>(prepared_ms),
              static_cast<long long>(elapsed_ms()));
  return {};
}

auto execute_window_focus_request(
    const control::window_focus_request &request) -> std::expected<void, control::error> {
  std::vector<display_record> displays;
  if (!load_active_displays(displays)) {
    return std::unexpected(state_error("Failed to enumerate active displays."));
  }

  const auto active_display_uuid = load_active_display_uuid();
  if (!active_display_uuid) {
    return std::unexpected(state_error("Failed to determine the active display."));
  }

  const auto current_index = find_current_display_index(displays, *active_display_uuid);
  if (!current_index) {
    return std::unexpected(state_error("Failed to match the active display."));
  }

  std::vector<window_record> windows;
  if (!load_on_screen_windows(windows)) {
    return std::unexpected(runtime_error("Failed to enumerate on-screen windows."));
  }

  const auto current_window_ids =
      collect_window_ids_on_display(std::span{windows}, displays[*current_index].bounds);

  const auto now_ms = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());

  std::optional<window_cycle_state> previous_state;
  {
    std::lock_guard lock(window_cycle_state_mutex);
    previous_state = window_cycle_state_cache;
  }

  const auto plan = plan_window_focus_request(
      request, std::span{current_window_ids}, previous_state, now_ms, window_cycle_timeout_ms);

  {
    std::lock_guard lock(window_cycle_state_mutex);
    window_cycle_state_cache = plan.next_state;
  }

  if (!plan.target_window_id) {
    return {};
  }

  const auto target_window_index =
      find_window_index_by_id(std::span{windows}, *plan.target_window_id);
  if (!target_window_index) {
    return std::unexpected(runtime_error("Failed to match the target window."));
  }

  if (!focus_window(windows[*target_window_index], *active_display_uuid)) {
    return std::unexpected(runtime_error("Failed to focus the target window."));
  }

  return {};
}

}  // namespace spacehound::control::detail
