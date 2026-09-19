#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <variant>

namespace spacehound::control {

enum class error_code {
  permission_denied,
  invalid_request,
  state_unavailable,
  runtime_error,
};

struct error final {
  error_code code = error_code::runtime_error;
  std::string message;

  [[nodiscard]] auto operator==(const error &) const -> bool = default;
};

enum class workspace_action {
  left,
  right,
  go_to,
};

struct workspace_request final {
  workspace_action action = workspace_action::left;
  std::size_t index = 0;
  bool wrap = false;
  bool move_cursor_to_active_display = true;

  [[nodiscard]] auto operator==(const workspace_request &) const -> bool = default;
};

enum class display_action {
  left,
  right,
  go_to,
};

struct display_request final {
  display_action action = display_action::left;
  std::size_t index = 0;
  bool wrap = false;

  [[nodiscard]] auto operator==(const display_request &) const -> bool = default;
};

enum class system_ui_element {
  mission_control,
  expose,
};

struct system_ui_request final {
  system_ui_element element = system_ui_element::mission_control;

  [[nodiscard]] auto operator==(const system_ui_request &) const -> bool = default;
};

enum class window_focus_direction {
  next,
  previous,
};

struct window_focus_request final {
  window_focus_direction direction = window_focus_direction::next;

  [[nodiscard]] auto operator==(const window_focus_request &) const -> bool = default;
};

using request =
    std::variant<workspace_request, display_request, system_ui_request, window_focus_request>;

[[nodiscard]] auto execute(const request &request)
    -> std::expected<void, error>;

}  // namespace spacehound::control
