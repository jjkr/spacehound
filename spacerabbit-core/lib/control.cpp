#include <spacerabbit/control.hpp>

#include "internal/control_internal.hpp"

#include <ApplicationServices/ApplicationServices.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <spacerabbit/ax.hpp>
#include <spacerabbit/cf.hpp>
#include <spacerabbit/cg.hpp>
#include <spacerabbit/cgs.hpp>
#include <spacerabbit/gesture.hpp>

namespace spacerabbit::control {
namespace {

namespace ax = spacerabbit::ax;
namespace cf = spacerabbit::cf;
namespace cg = spacerabbit::cg;
namespace cgs = spacerabbit::cgs;
namespace detail = spacerabbit::control::detail;
namespace gesture = spacerabbit::gesture;

inline const auto display_identifier_key = cf::string_view{CFSTR("Display Identifier")};
inline const auto spaces_key = cf::string_view{CFSTR("Spaces")};
inline const auto current_space_key = cf::string_view{CFSTR("Current Space")};
inline const auto managed_space_id_key = cf::string_view{CFSTR("ManagedSpaceID")};
constexpr auto all_windows_option = static_cast<CGWindowListOption>(kCGWindowListOptionAll);

enum class dock_view_state {
  hidden,
  mission_control,
  expose,
};

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

auto active_display_space_bounds(
    detail::workspace_bounds &out_bounds,
    std::optional<CGRect> &out_display_bounds) -> bool {
  const auto connection = cgs::main_connection_id();
  const auto active_display = cgs::copy_active_menu_bar_display_identifier(connection);
  if (!active_display) {
    return false;
  }

  const auto active_display_utf8 = active_display.to_utf8();
  if (!active_display_utf8) {
    return false;
  }

  out_display_bounds.reset();
  if (!detail::is_unified_spaces_display_identifier(*active_display_utf8)) {
    CGRect display_bounds{};
    if (!display_bounds_for_identifier(active_display.view(), display_bounds)) {
      return false;
    }
    out_display_bounds = display_bounds;
  }

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
    const auto display_identifier = display_dict.find<CFStringRef>(display_identifier_key);
    if (!display_identifier || !active_display.equals(cf::string_view{display_identifier})) {
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
    std::size_t repeat_count) -> bool {
  for (std::size_t index = 0; index < repeat_count; ++index) {
    if (!gesture::post_swipe(synthetic_source, direction)) {
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

  detail::workspace_bounds bounds{};
  std::optional<CGRect> display_bounds;
  if (!active_display_space_bounds(bounds, display_bounds)) {
    return std::unexpected(state_error("Failed to determine the active display workspace state."));
  }

  const auto motion =
      detail::plan_workspace_request(request, bounds.current_index, bounds.num_spaces);
  if (!motion.should_execute) {
    return {};
  }

  if (display_bounds &&
      !detail::ensure_cursor_on_display(synthetic_source, *display_bounds)) {
    return std::unexpected(runtime_error("Failed to move the cursor to the active display."));
  }

  if (!post_swipe_sequence(synthetic_source, motion.direction, motion.repeat_count)) {
    return std::unexpected(runtime_error("Failed to synthesize the workspace gesture sequence."));
  }

  return {};
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

auto subtree_contains_identifier(
    ax::ui_element_view element,
    std::string_view identifier,
    std::size_t depth = 0) -> bool {
  if (!element || depth > 12) {
    return false;
  }

  const auto element_identifier = ax_identifier(element);
  if (element_identifier && *element_identifier == identifier) {
    return true;
  }

  cf::type value;
  if (element.copy_attribute_value(ax::children_attribute, value) != kAXErrorSuccess || !value) {
    return false;
  }

  const auto children = value.cast<CFArrayRef>();
  if (!children) {
    return false;
  }

  const auto count = CFArrayGetCount(children.get());
  for (CFIndex index = 0; index < count; ++index) {
    const auto child = cf::array_at<AXUIElementRef>(children, index);
    if (!child) {
      continue;
    }

    if (subtree_contains_identifier(ax::ui_element_view{child}, identifier, depth + 1)) {
      return true;
    }
  }

  return false;
}

auto detect_dock_view_state() -> std::optional<dock_view_state> {
  const auto pid = dock_pid();
  if (!pid) {
    return std::nullopt;
  }

  const auto dock = ax::ui_element::create_application(*pid);
  if (!dock) {
    return std::nullopt;
  }

  if (subtree_contains_identifier(dock.view(), "appexpose")) {
    return dock_view_state::expose;
  }

  if (subtree_contains_identifier(dock.view(), "mc") ||
      subtree_contains_identifier(dock.view(), "mc.spaces")) {
    return dock_view_state::mission_control;
  }

  return dock_view_state::hidden;
}

auto toggle_direction(system_ui_request request, dock_view_state state) -> gesture::direction {
  switch (request.element) {
    case system_ui_element::mission_control:
      return state == dock_view_state::mission_control ? gesture::direction::down
                                                       : gesture::direction::up;
    case system_ui_element::expose:
      return state == dock_view_state::expose ? gesture::direction::up
                                              : gesture::direction::down;
  }

  return gesture::direction::up;
}

auto execute_system_ui_request(
    const system_ui_request &request,
    cg::event_source_view synthetic_source) -> std::expected<void, error> {
  const auto state = detect_dock_view_state();
  if (!state) {
    return std::unexpected(state_error("Failed to inspect the Dock accessibility hierarchy."));
  }

  if (!gesture::post_swipe(synthetic_source, toggle_direction(request, *state))) {
    return std::unexpected(runtime_error("Failed to synthesize the system UI gesture."));
  }

  return {};
}

}  // namespace

namespace detail {

auto is_unified_spaces_display_identifier(
    std::string_view display_identifier) noexcept -> bool {
  return display_identifier == "Main";
}

auto action_name(const control::request &request) noexcept -> std::string_view {
  return std::visit(
      [](const auto &typed_request) -> std::string_view {
        using request_type = std::decay_t<decltype(typed_request)>;
        if constexpr (std::is_same_v<request_type, workspace_request>) {
          return "workspace";
        } else if constexpr (std::is_same_v<request_type, display_request>) {
          return "display";
        } else if constexpr (std::is_same_v<request_type, window_focus_request>) {
          return "window-focus";
        } else {
          return "system-ui";
        }
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
  const auto permission = ensure_accessibility_permission();
  if (!permission) {
    return std::unexpected(permission.error());
  }

  return std::visit(
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
          return execute_window_focus_request(typed_request);
        } else {
          return execute_system_ui_request(typed_request, synthetic_source);
        }
      },
      request);
}

}  // namespace detail

auto execute(const request &request) -> std::expected<void, error> {
  const auto synthetic_source =
      cg::event_source::create(kCGEventSourceStateHIDSystemState);
  if (!synthetic_source) {
    return std::unexpected(runtime_error("Failed to create a synthetic Core Graphics event source."));
  }

  return detail::execute_request(request, synthetic_source.view());
}

}  // namespace spacerabbit::control
