#include <spacehound/control.hpp>

#include "internal/control_internal.hpp"
#include "internal/logging.hpp"

#include <ApplicationServices/ApplicationServices.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <spacehound/ax.hpp>
#include <spacehound/cf.hpp>
#include <spacehound/cg.hpp>
#include <spacehound/cgs.hpp>
#include <spacehound/gesture.hpp>

namespace spacehound::control {
namespace {

namespace ax = spacehound::ax;
namespace cf = spacehound::cf;
namespace cg = spacehound::cg;
namespace cgs = spacehound::cgs;
namespace detail = spacehound::control::detail;
namespace gesture = spacehound::gesture;
namespace diagnostics = spacehound::diagnostics;

inline const auto display_identifier_key = cf::string_view{CFSTR("Display Identifier")};
inline const auto spaces_key = cf::string_view{CFSTR("Spaces")};
inline const auto current_space_key = cf::string_view{CFSTR("Current Space")};
inline const auto managed_space_id_key = cf::string_view{CFSTR("ManagedSpaceID")};
constexpr auto all_windows_option = static_cast<CGWindowListOption>(kCGWindowListOptionAll);

auto make_error(error_code code, std::string message) -> error {
  return error{
      .code = code,
      .message = std::move(message),
  };
}

auto permission_error(std::string message) -> error {
  return make_error(error_code::permission_denied, std::move(message));
}

auto invalid_request_error(std::string message) -> error {
  return make_error(error_code::invalid_request, std::move(message));
}

auto state_error(std::string message) -> error {
  return make_error(error_code::state_unavailable, std::move(message));
}

auto runtime_error(std::string message) -> error {
  return make_error(error_code::runtime_error, std::move(message));
}

auto rect_center(CGRect rect) noexcept -> CGPoint {
  return CGPoint{
      .x = rect.origin.x + rect.size.width / 2.0,
      .y = rect.origin.y + rect.size.height / 2.0,
  };
}

auto error_code_name(error_code code) noexcept -> const char * {
  switch (code) {
    case error_code::permission_denied:
      return "permission-denied";
    case error_code::invalid_request:
      return "invalid-request";
    case error_code::state_unavailable:
      return "state-unavailable";
    case error_code::runtime_error:
      return "runtime-error";
  }

  return "unknown";
}

auto dictionary_number_int64(
    cf::dictionary_view dictionary,
    cf::string_view key,
    std::int64_t &out_value) -> bool {
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

auto display_bounds_for_identifier(
    cf::string_view display_identifier,
    CGRect &out_bounds) -> bool {
  std::vector<CGDirectDisplayID> display_ids;
  if (cg::active_displays(display_ids) != kCGErrorSuccess) {
    return false;
  }

  for (const auto display_id : display_ids) {
    const auto identifier = cg::display_uuid_string(display_id);
    if (identifier && identifier.equals(display_identifier)) {
      out_bounds = cg::display_bounds(display_id);
      return true;
    }
  }

  return false;
}

// Looks up the current space index and space count for the display with the
// given Spaces identifier (a display UUID, or "Main" in unified-spaces mode).
auto space_bounds_for_display(
    cf::string_view display_identifier,
    detail::workspace_bounds &out_bounds) -> bool {
  const auto connection = cgs::main_connection_id();
  const auto managed_spaces = cgs::copy_managed_display_spaces(connection);
  if (!managed_spaces) {
    return false;
  }

  const auto managed_spaces_view = cf::view<CFArrayRef>{managed_spaces.get()};
  const auto display_count = CFArrayGetCount(managed_spaces_view.get());
  for (CFIndex display_index = 0; display_index < display_count; ++display_index) {
    const auto display_dict_ref =
        cf::array_at<CFDictionaryRef>(managed_spaces_view, display_index);
    if (!display_dict_ref) {
      continue;
    }

    const auto display_dict = cf::dictionary_view{display_dict_ref};
    const auto entry_identifier = display_dict.find<CFStringRef>(display_identifier_key);
    if (!entry_identifier || !display_identifier.equals(cf::string_view{entry_identifier})) {
      continue;
    }

    const auto spaces = display_dict.find<CFArrayRef>(spaces_key);
    const auto current_space_ref = display_dict.find<CFDictionaryRef>(current_space_key);
    if (!spaces || !current_space_ref) {
      break;
    }

    std::int64_t current_space_id = 0;
    if (!dictionary_number_int64(
            cf::dictionary_view{current_space_ref}, managed_space_id_key, current_space_id)) {
      break;
    }

    const auto space_count = CFArrayGetCount(spaces.get());
    for (CFIndex space_index = 0; space_index < space_count; ++space_index) {
      const auto space_dict_ref = cf::array_at<CFDictionaryRef>(spaces, space_index);
      if (!space_dict_ref) {
        continue;
      }

      std::int64_t space_id = 0;
      if (!dictionary_number_int64(
              cf::dictionary_view{space_dict_ref}, managed_space_id_key, space_id)) {
        continue;
      }

      if (space_id == current_space_id) {
        out_bounds.current_index = static_cast<std::int64_t>(space_index);
        out_bounds.num_spaces = static_cast<std::int64_t>(space_count);
        return true;
      }
    }

    break;
  }

  return false;
}

// The display a workspace request operates on: its Spaces identifier plus, when
// it is not the display under the cursor, a point on it to report the gesture
// at. `gesture_location` stays empty in unified-spaces mode and when the
// request follows the cursor, in which case the gesture lands wherever the
// cursor is.
struct workspace_display final {
  cf::string identifier;
  std::optional<CGPoint> gesture_location;
};

enum class workspace_display_error {
  active_display_unavailable,
  cursor_display_unavailable,
};

auto resolve_workspace_display(
    const workspace_request &request,
    cg::event_source_view synthetic_source)
    -> std::expected<workspace_display, workspace_display_error> {
  const auto active_display_utf8 = detail::active_display_identifier();
  if (!active_display_utf8) {
    return std::unexpected(workspace_display_error::active_display_unavailable);
  }

  auto active_display = cf::string::from_utf8(*active_display_utf8);
  if (!active_display) {
    return std::unexpected(workspace_display_error::active_display_unavailable);
  }

  if (detail::is_unified_spaces_display_identifier(*active_display_utf8)) {
    return workspace_display{.identifier = std::move(active_display)};
  }

  if (request.target_focused_display) {
    CGRect display_bounds{};
    if (!display_bounds_for_identifier(active_display.view(), display_bounds)) {
      return std::unexpected(workspace_display_error::active_display_unavailable);
    }

    return workspace_display{
        .identifier = std::move(active_display),
        .gesture_location = rect_center(display_bounds),
    };
  }

  // Follow the cursor: switch spaces on whichever display it is on.
  const auto cursor_event = cg::event::create(synthetic_source);
  if (!cursor_event) {
    return std::unexpected(workspace_display_error::cursor_display_unavailable);
  }

  std::vector<detail::display_record> displays;
  if (!detail::load_active_displays(displays)) {
    return std::unexpected(workspace_display_error::cursor_display_unavailable);
  }

  const auto cursor_display_index =
      detail::find_display_index_containing_point(displays, cursor_event.location());
  if (!cursor_display_index) {
    return std::unexpected(workspace_display_error::cursor_display_unavailable);
  }

  auto identifier = cf::string::from_utf8(displays[*cursor_display_index].uuid);
  if (!identifier) {
    return std::unexpected(workspace_display_error::cursor_display_unavailable);
  }

  return workspace_display{.identifier = std::move(identifier)};
}

auto ensure_accessibility_permission() -> std::expected<void, error> {
  if (ax::is_process_trusted()) {
    return {};
  }

  return std::unexpected(permission_error(
      "Accessibility permission is required. Enable it in System Settings > Privacy & Security > Accessibility and try again."));
}

auto post_swipe_sequence(
    cg::event_source_view synthetic_source,
    gesture::direction direction,
    std::size_t repeat_count,
    const gesture::swipe_options &options) -> bool {
  for (std::size_t index = 0; index < repeat_count; ++index) {
    if (!gesture::post_swipe(synthetic_source, direction, kCGHIDEventTap, options)) {
      return false;
    }
  }

  return true;
}


auto execute_workspace_request(
    const workspace_request &request,
    cg::event_source_view synthetic_source) -> std::expected<void, error> {
  if (request.action == workspace_action::go_to && request.index == 0) {
    return std::unexpected(invalid_request_error("Workspace indices are 1-based."));
  }

  const auto display = resolve_workspace_display(request, synthetic_source);
  if (!display) {
    switch (display.error()) {
      case workspace_display_error::active_display_unavailable:
        return std::unexpected(state_error("Failed to determine the active display."));
      case workspace_display_error::cursor_display_unavailable:
        return std::unexpected(state_error("Failed to determine the display under the cursor."));
    }
  }

  detail::workspace_bounds bounds{};
  if (!space_bounds_for_display(display->identifier.view(), bounds)) {
    return std::unexpected(state_error("Failed to determine the display workspace state."));
  }

  const auto motion =
      detail::plan_workspace_request(request, bounds.current_index, bounds.num_spaces);
  if (!motion.should_execute) {
    return {};
  }

  const gesture::swipe_options swipe_options{.location = display->gesture_location};
  if (!post_swipe_sequence(
          synthetic_source, motion.direction, motion.repeat_count, swipe_options)) {
    return std::unexpected(runtime_error("Failed to synthesize the workspace gesture sequence."));
  }

  return {};
}

auto ax_identifier(ax::ui_element_view element) -> std::optional<std::string> {
  cf::type value;
  if (element.copy_attribute_value(ax::identifier_attribute, value) != kAXErrorSuccess || !value) {
    return std::nullopt;
  }

  const auto identifier = value.cast<CFStringRef>();
  if (!identifier) {
    return std::nullopt;
  }

  return cf::string_view{identifier}.to_utf8();
}

// Returns the first identifier from `identifiers` found anywhere in the
// subtree, walking depth-first and at most `max_depth` levels down.
auto find_first_identifier(
    ax::ui_element_view element,
    std::span<const std::string_view> identifiers,
    std::size_t depth = 0) -> std::optional<std::string_view> {
  constexpr std::size_t max_depth = 12;
  if (!element || depth > max_depth) {
    return std::nullopt;
  }

  if (const auto element_identifier = ax_identifier(element)) {
    const auto match = std::ranges::find(identifiers, *element_identifier);
    if (match != identifiers.end()) {
      return *match;
    }
  }

  cf::type value;
  if (element.copy_attribute_value(ax::children_attribute, value) != kAXErrorSuccess || !value) {
    return std::nullopt;
  }

  const auto children = value.cast<CFArrayRef>();
  if (!children) {
    return std::nullopt;
  }

  const auto count = CFArrayGetCount(children.get());
  for (CFIndex index = 0; index < count; ++index) {
    const auto child = cf::array_at<AXUIElementRef>(children, index);
    if (!child) {
      continue;
    }

    if (const auto found = find_first_identifier(ax::ui_element_view{child}, identifiers, depth + 1)) {
      return found;
    }
  }

  return std::nullopt;
}

auto toggle_direction(system_ui_request request, detail::dock_view_state state) -> gesture::direction {
  switch (request.element) {
    case system_ui_element::mission_control:
      return state == detail::dock_view_state::mission_control ? gesture::direction::down
                                                       : gesture::direction::up;
    case system_ui_element::expose:
      return state == detail::dock_view_state::expose ? gesture::direction::up
                                              : gesture::direction::down;
  }

  return gesture::direction::up;
}

auto execute_system_ui_request(
    const system_ui_request &request,
    cg::event_source_view synthetic_source) -> std::expected<void, error> {
  const auto pid = detail::dock_pid();
  const auto state = pid ? detail::detect_dock_view_state(*pid) : std::nullopt;
  if (!state) {
    return std::unexpected(state_error("Failed to inspect the Dock accessibility hierarchy."));
  }

  const auto direction = toggle_direction(request, *state);
  const bool dismisses_overlay =
      (*state == detail::dock_view_state::mission_control &&
       direction == gesture::direction::down) ||
      (*state == detail::dock_view_state::expose && direction == gesture::direction::up);

  if (dismisses_overlay) {
    // The overlay activates the thumbnail under the cursor as it closes:
    // put the (hidden) cursor on the highlighted one first.
    detail::prepare_overlay_dismissal(synthetic_source);
  }

  if (!gesture::post_swipe(synthetic_source, direction)) {
    return std::unexpected(runtime_error("Failed to synthesize the system UI gesture."));
  }

  if (*state == detail::dock_view_state::hidden) {
    // Opening an overlay: highlight the focused window once it has appeared.
    detail::highlight_frontmost_window_when_overlay_appears();
  }

  return {};
}

}  // namespace

namespace detail {

auto dock_view_state_name(dock_view_state state) noexcept -> std::string_view {
  switch (state) {
    case dock_view_state::hidden:
      return "hidden";
    case dock_view_state::mission_control:
      return "mission-control";
    case dock_view_state::expose:
      return "expose";
  }

  return "unknown";
}

auto dock_pid() -> std::optional<pid_t> {
  const auto windows = cg::copy_window_info(all_windows_option, kCGNullWindowID);
  if (!windows) {
    return std::nullopt;
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
    if (!owner_name) {
      continue;
    }

    const auto utf8 = cf::string_view{owner_name}.to_utf8();
    if (!utf8 || *utf8 != "Dock") {
      continue;
    }

    std::int64_t pid = 0;
    if (dictionary_number_int64(window_dict, cg::window_owner_pid_key, pid) && pid > 0) {
      return static_cast<pid_t>(pid);
    }
  }

  return std::nullopt;
}

auto detect_dock_view_state(pid_t dock_pid) -> std::optional<dock_view_state> {
  const auto dock = ax::ui_element::create_application(dock_pid);
  if (!dock) {
    return std::nullopt;
  }

  // One walk for all three markers: this runs on every window-cycle hotkey.
  static constexpr std::string_view markers[] = {"appexpose", "mc", "mc.spaces"};
  const auto found = find_first_identifier(dock.view(), markers);
  if (!found) {
    return dock_view_state::hidden;
  }

  return *found == "appexpose" ? dock_view_state::expose : dock_view_state::mission_control;
}

auto is_unified_spaces_display_identifier(
    std::string_view display_identifier) noexcept -> bool {
  return display_identifier == "Main";
}

auto action_name(const control::request &request) noexcept -> std::string_view {
  return std::visit(
      [](const auto &typed_request) -> std::string_view {
        using request_type = std::decay_t<decltype(typed_request)>;
        if constexpr (std::is_same_v<request_type, workspace_request>) {
          switch (typed_request.action) {
            case workspace_action::left:
              return "workspace-left";
            case workspace_action::right:
              return "workspace-right";
            case workspace_action::go_to:
              return "workspace-go-to";
          }
        } else if constexpr (std::is_same_v<request_type, display_request>) {
          switch (typed_request.action) {
            case display_action::left:
              return "display-left";
            case display_action::right:
              return "display-right";
            case display_action::go_to:
              return "display-go-to";
          }
        } else if constexpr (std::is_same_v<request_type, window_focus_request>) {
          return typed_request.direction == window_focus_direction::next
                     ? "window-focus-next"
                     : "window-focus-previous";
        } else {
          return typed_request.element == system_ui_element::mission_control
                     ? "system-ui-mission-control"
                     : "system-ui-expose";
        }

        return "unknown";
      },
      request);
}

auto plan_workspace_request(
    const control::workspace_request &request,
    std::int64_t current_index,
    std::int64_t num_spaces) noexcept -> workspace_motion {
  if (num_spaces <= 0 || current_index < 0 || current_index >= num_spaces) {
    return {};
  }

  switch (request.action) {
    case control::workspace_action::left:
      if (current_index == 0) {
        if (!request.wrap || num_spaces <= 1) {
          return {};
        }

        return workspace_motion{
            .should_execute = true,
            .direction = gesture::direction::right,
            .repeat_count = static_cast<std::size_t>(num_spaces - 1),
        };
      }

      return workspace_motion{
          .should_execute = true,
          .direction = gesture::direction::left,
          .repeat_count = 1,
      };
    case control::workspace_action::right:
      if (current_index >= num_spaces - 1) {
        if (!request.wrap || num_spaces <= 1) {
          return {};
        }

        return workspace_motion{
            .should_execute = true,
            .direction = gesture::direction::left,
            .repeat_count = static_cast<std::size_t>(num_spaces - 1),
        };
      }

      return workspace_motion{
          .should_execute = true,
          .direction = gesture::direction::right,
          .repeat_count = 1,
      };
    case control::workspace_action::go_to: {
      if (request.index == 0) {
        return {};
      }

      const auto target_index = static_cast<std::int64_t>(request.index - 1);
      if (target_index < 0 || target_index >= num_spaces || target_index == current_index) {
        return {};
      }

      if (target_index > current_index) {
        return workspace_motion{
            .should_execute = true,
            .direction = gesture::direction::right,
            .repeat_count = static_cast<std::size_t>(target_index - current_index),
        };
      }

      return workspace_motion{
          .should_execute = true,
          .direction = gesture::direction::left,
          .repeat_count = static_cast<std::size_t>(current_index - target_index),
      };
    }
  }

  return {};
}

auto execute_request(
    const control::request &request,
    cg::event_source_view synthetic_source) -> std::expected<void, control::error> {
  const auto name = action_name(request);
  os_log_debug(diagnostics::navigation_log(),
               "Action execution started (type=%{public}s)",
               name.data());

  const auto permission = ensure_accessibility_permission();
  if (!permission) {
    os_log_error(diagnostics::navigation_log(),
                 "Action execution failed (type=%{public}s code=%{public}s)",
                 name.data(),
                 error_code_name(permission.error().code));
    return std::unexpected(permission.error());
  }

  auto result = std::visit(
      [&](const auto &typed_request) -> std::expected<void, control::error> {
        using request_type = std::decay_t<decltype(typed_request)>;
        if constexpr (std::is_same_v<request_type, workspace_request>) {
          return execute_workspace_request(typed_request, synthetic_source);
        } else if constexpr (std::is_same_v<request_type, display_request>) {
          if (typed_request.action == display_action::go_to && typed_request.index == 0) {
            return std::unexpected(invalid_request_error("Display indices are 1-based."));
          }

          return execute_display_request(typed_request, synthetic_source);
        } else if constexpr (std::is_same_v<request_type, window_focus_request>) {
          return execute_window_focus_request(typed_request, synthetic_source);
        } else {
          return execute_system_ui_request(typed_request, synthetic_source);
        }
      },
      request);

  if (result.has_value()) {
    os_log_debug(diagnostics::navigation_log(),
                 "Action execution completed (type=%{public}s)",
                 name.data());
  } else {
    os_log_error(diagnostics::navigation_log(),
                 "Action execution failed (type=%{public}s code=%{public}s)",
                 name.data(),
                 error_code_name(result.error().code));
  }

  return result;
}

}  // namespace detail

auto execute(const request &request) -> std::expected<void, error> {
  const auto synthetic_source =
      cg::event_source::create(kCGEventSourceStateHIDSystemState);
  if (!synthetic_source) {
    const auto name = detail::action_name(request);
    os_log_error(diagnostics::navigation_log(),
                 "Action setup failed (type=%{public}s code=event-source-unavailable)",
                 name.data());
    return std::unexpected(runtime_error("Failed to create a synthetic Core Graphics event source."));
  }

  return detail::execute_request(request, synthetic_source.view());
}

}  // namespace spacehound::control
