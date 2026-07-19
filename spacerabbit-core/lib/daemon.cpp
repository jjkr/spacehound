#include <spacerabbit/daemon.hpp>

#include "internal/control_internal.hpp"
#include "internal/daemon_internal.hpp"
#include "internal/logging.hpp"

#include <Carbon/Carbon.h>
#include <dispatch/source.h>
#include <signal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spacerabbit/ax.hpp>
#include <spacerabbit/cf.hpp>
#include <spacerabbit/cg.hpp>
#include <spacerabbit/control.hpp>
#include <spacerabbit/cgs.hpp>
#include <spacerabbit/dispatch.hpp>
#include <spacerabbit/gesture.hpp>
#include <spacerabbit/ns.hpp>
#include <spacerabbit/settings.hpp>

namespace spacerabbit::daemon {
namespace {

namespace ax = spacerabbit::ax;
namespace cf = spacerabbit::cf;
namespace cg = spacerabbit::cg;
namespace control = spacerabbit::control;
namespace cgs = spacerabbit::cgs;
namespace dispatch = spacerabbit::dispatch;
namespace diagnostics = spacerabbit::diagnostics;
namespace gesture = spacerabbit::gesture;
namespace ns = spacerabbit::ns;
namespace settings = spacerabbit::settings;

inline constexpr auto relevant_modifier_mask = kCGEventFlagMaskCommand |
                                               kCGEventFlagMaskControl |
                                               kCGEventFlagMaskAlternate |
                                               kCGEventFlagMaskShift;
inline const auto display_identifier_key = cf::string_view{CFSTR("Display Identifier")};
inline const auto spaces_key = cf::string_view{CFSTR("Spaces")};
inline const auto current_space_key = cf::string_view{CFSTR("Current Space")};
inline const auto managed_space_id_key = cf::string_view{CFSTR("ManagedSpaceID")};

struct space_bounds final {
  std::int64_t current_index = 0;
  std::int64_t num_spaces = 0;
};

struct runtime_context final {
  detail::runtime_config config;
  cg::event_tap_view tap{};
  cg::event_source synthetic_source{};
  // When set, the event tap stays installed but passes every event through
  // untouched, so a shortcut editor can capture combinations that would
  // otherwise trigger a hotkey or gesture.
  std::atomic<bool> input_suspended{false};
};

auto make_error(error_code code, std::string message) -> error {
  return error{
      .code = code,
      .message = std::move(message),
  };
}

auto settings_error(std::string message) -> error {
  return make_error(error_code::settings_error, std::move(message));
}

auto permission_error(std::string message) -> error {
  return make_error(error_code::permission_denied, std::move(message));
}

auto state_error(std::string message) -> error {
  return make_error(error_code::state_unavailable, std::move(message));
}

auto runtime_error(std::string message) -> error {
  return make_error(error_code::runtime_error, std::move(message));
}

auto already_running_error() -> error {
  return make_error(error_code::already_running, "SpaceRabbit daemon runtime is already running.");
}

auto not_running_error() -> error {
  return make_error(error_code::not_running, "SpaceRabbit daemon runtime is not running.");
}

auto lowercase(std::string_view value) -> std::string {
  std::string result;
  result.reserve(value.size());
  for (const char ch : value) {
    result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return result;
}

auto gesture_direction_name(gesture::direction value) -> std::string_view {
  switch (value) {
    case gesture::direction::left:
      return "left";
    case gesture::direction::right:
      return "right";
    case gesture::direction::up:
      return "up";
    case gesture::direction::down:
      return "down";
  }

  return "unknown";
}

auto is_horizontal_direction(gesture::direction value) -> bool {
  return value == gesture::direction::left || value == gesture::direction::right;
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

auto active_display_space_bounds(space_bounds &out_bounds) -> bool {
  const auto connection = cgs::main_connection_id();
  const auto active_display = cgs::copy_active_menu_bar_display_identifier(connection);
  if (!active_display) {
    return false;
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
    const auto display_identifier =
        display_dict.find<CFStringRef>(display_identifier_key);
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

auto make_workspace_state(space_bounds bounds) -> workspace_state {
  return workspace_state{
      .current_space = static_cast<std::size_t>(bounds.current_index + 1),
      .num_spaces = static_cast<std::size_t>(bounds.num_spaces),
  };
}

auto read_workspace_state() -> std::expected<workspace_state, error> {
  space_bounds bounds{};
  if (!active_display_space_bounds(bounds)) {
    return std::unexpected(
        state_error("Failed to determine the active display workspace state."));
  }

  return make_workspace_state(bounds);
}

auto normalized_flags(CGEventFlags flags) -> CGEventFlags {
  return flags & relevant_modifier_mask;
}

auto parse_modifier_flags(
    const std::vector<std::string> &modifiers,
    CGEventFlags &out_flags) -> bool {
  CGEventFlags flags = 0;
  for (const auto &modifier : modifiers) {
    const auto normalized = lowercase(modifier);
    if (normalized == "ctrl") {
      flags |= kCGEventFlagMaskControl;
      continue;
    }

    if (normalized == "option" || normalized == "alt") {
      flags |= kCGEventFlagMaskAlternate;
      continue;
    }

    if (normalized == "cmd" || normalized == "command" || normalized == "meta") {
      flags |= kCGEventFlagMaskCommand;
      continue;
    }

    if (normalized == "shift") {
      flags |= kCGEventFlagMaskShift;
      continue;
    }

    return false;
  }

  out_flags = flags;
  return true;
}

auto parse_key_code(std::string_view key, CGKeyCode &out_key_code) -> bool {
  const auto normalized = lowercase(key);

  using key_mapping = std::pair<std::string_view, CGKeyCode>;
  static constexpr key_mapping named_keys[] = {
      {"tab", static_cast<CGKeyCode>(kVK_Tab)},
      {"return", static_cast<CGKeyCode>(kVK_Return)},
      {"enter", static_cast<CGKeyCode>(kVK_Return)},
      {"space", static_cast<CGKeyCode>(kVK_Space)},
      {"escape", static_cast<CGKeyCode>(kVK_Escape)},
      {"esc", static_cast<CGKeyCode>(kVK_Escape)},
      {"delete", static_cast<CGKeyCode>(kVK_Delete)},
      {"backspace", static_cast<CGKeyCode>(kVK_Delete)},
      {"left", static_cast<CGKeyCode>(kVK_LeftArrow)},
      {"arrowleft", static_cast<CGKeyCode>(kVK_LeftArrow)},
      {"right", static_cast<CGKeyCode>(kVK_RightArrow)},
      {"arrowright", static_cast<CGKeyCode>(kVK_RightArrow)},
      {"up", static_cast<CGKeyCode>(kVK_UpArrow)},
      {"arrowup", static_cast<CGKeyCode>(kVK_UpArrow)},
      {"down", static_cast<CGKeyCode>(kVK_DownArrow)},
      {"arrowdown", static_cast<CGKeyCode>(kVK_DownArrow)},
      {"[", static_cast<CGKeyCode>(kVK_ANSI_LeftBracket)},
      {"leftbracket", static_cast<CGKeyCode>(kVK_ANSI_LeftBracket)},
      {"]", static_cast<CGKeyCode>(kVK_ANSI_RightBracket)},
      {"rightbracket", static_cast<CGKeyCode>(kVK_ANSI_RightBracket)},
      {"-", static_cast<CGKeyCode>(kVK_ANSI_Minus)},
      {"minus", static_cast<CGKeyCode>(kVK_ANSI_Minus)},
      {"=", static_cast<CGKeyCode>(kVK_ANSI_Equal)},
      {"equal", static_cast<CGKeyCode>(kVK_ANSI_Equal)},
  };

  for (const auto &[name, key_code] : named_keys) {
    if (normalized == name) {
      out_key_code = key_code;
      return true;
    }
  }

  if (normalized.size() == 1) {
    switch (normalized.front()) {
      case 'a':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_A);
        return true;
      case 'b':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_B);
        return true;
      case 'c':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_C);
        return true;
      case 'd':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_D);
        return true;
      case 'e':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_E);
        return true;
      case 'f':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_F);
        return true;
      case 'g':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_G);
        return true;
      case 'h':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_H);
        return true;
      case 'i':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_I);
        return true;
      case 'j':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_J);
        return true;
      case 'k':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_K);
        return true;
      case 'l':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_L);
        return true;
      case 'm':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_M);
        return true;
      case 'n':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_N);
        return true;
      case 'o':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_O);
        return true;
      case 'p':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_P);
        return true;
      case 'q':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_Q);
        return true;
      case 'r':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_R);
        return true;
      case 's':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_S);
        return true;
      case 't':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_T);
        return true;
      case 'u':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_U);
        return true;
      case 'v':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_V);
        return true;
      case 'w':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_W);
        return true;
      case 'x':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_X);
        return true;
      case 'y':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_Y);
        return true;
      case 'z':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_Z);
        return true;
      case '0':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_0);
        return true;
      case '1':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_1);
        return true;
      case '2':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_2);
        return true;
      case '3':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_3);
        return true;
      case '4':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_4);
        return true;
      case '5':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_5);
        return true;
      case '6':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_6);
        return true;
      case '7':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_7);
        return true;
      case '8':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_8);
        return true;
      case '9':
        out_key_code = static_cast<CGKeyCode>(kVK_ANSI_9);
        return true;
      default:
        break;
    }
  }

  return false;
}

auto compile_action(
    std::string_view action_id,
    const settings::hotkey_setting &hotkey,
    const settings::document &document) -> std::optional<detail::compiled_hotkey> {
  CGKeyCode key_code = 0;
  CGEventFlags modifier_flags = 0;
  if (!parse_key_code(hotkey.key, key_code) || !parse_modifier_flags(hotkey.modifiers, modifier_flags)) {
    return std::nullopt;
  }

  detail::compiled_hotkey result;
  result.action_id = std::string{action_id};
  result.key_code = key_code;
  result.modifier_flags = modifier_flags;

  if (action_id == "switch_space_left") {
    result.request = control::workspace_request{
        .action = control::workspace_action::left,
        .wrap = document.workspace_wrap,
    };
    return result;
  }

  if (action_id == "switch_space_right") {
    result.request = control::workspace_request{
        .action = control::workspace_action::right,
        .wrap = document.workspace_wrap,
    };
    return result;
  }

  if (std::string_view{action_id}.starts_with("switch_space_")) {
    const auto suffix = std::string_view{action_id}.substr(std::string_view{"switch_space_"}.size());
    std::size_t workspace_index = 0;
    const auto *first = suffix.data();
    const auto *last = suffix.data() + suffix.size();
    const auto [ptr, parse_error] = std::from_chars(first, last, workspace_index);
    if (parse_error == std::errc{} && ptr == last && workspace_index >= 1 && workspace_index <= 10) {
      result.request = control::workspace_request{
          .action = control::workspace_action::go_to,
          .index = workspace_index,
          .wrap = document.workspace_wrap,
      };
      return result;
    }
  }

  if (action_id == "switch_display_left") {
    result.request = control::display_request{
        .action = control::display_action::left,
        .wrap = document.display_wrap,
    };
    return result;
  }

  if (action_id == "switch_display_right") {
    result.request = control::display_request{
        .action = control::display_action::right,
        .wrap = document.display_wrap,
    };
    return result;
  }

  if (std::string_view{action_id}.starts_with("switch_display_")) {
    const auto suffix =
        std::string_view{action_id}.substr(std::string_view{"switch_display_"}.size());
    std::size_t display_index = 0;
    const auto *first = suffix.data();
    const auto *last = suffix.data() + suffix.size();
    const auto [ptr, parse_error] = std::from_chars(first, last, display_index);
    if (parse_error == std::errc{} && ptr == last && display_index >= 1 && display_index <= 10) {
      result.request = control::display_request{
          .action = control::display_action::go_to,
          .index = display_index,
          .wrap = document.display_wrap,
      };
      return result;
    }
  }

  if (action_id == "mission_control_toggle") {
    result.request = control::system_ui_request{
        .element = control::system_ui_element::mission_control,
    };
    return result;
  }

  if (action_id == "window_focus_next") {
    result.request = control::window_focus_request{
        .direction = control::window_focus_direction::next,
    };
    return result;
  }

  if (action_id == "window_focus_prev") {
    result.request = control::window_focus_request{
        .direction = control::window_focus_direction::previous,
    };
    return result;
  }

  if (action_id == "expose_toggle") {
    result.request = control::system_ui_request{
        .element = control::system_ui_element::expose,
    };
    return result;
  }

  return std::nullopt;
}

auto log_inert_actions(const detail::runtime_config &config) -> void {
  if (!config.inert_action_ids.empty()) {
    os_log_error(diagnostics::settings_log(),
                 "Runtime settings contain inert actions (count=%{public}lu)",
                 static_cast<unsigned long>(config.inert_action_ids.size()));
  }
}

auto hotkey_matches(const detail::compiled_hotkey &hotkey, cg::event_view event) -> bool {
  if (event.integer_field(kCGKeyboardEventAutorepeat) != 0) {
    return false;
  }

  const auto event_key_code =
      static_cast<CGKeyCode>(event.integer_field(kCGKeyboardEventKeycode));
  if (event_key_code != hotkey.key_code) {
    return false;
  }

  return normalized_flags(event.flags()) == hotkey.modifier_flags;
}

auto execute_hotkey(
    runtime_context &context,
    const detail::compiled_hotkey &hotkey) -> std::expected<void, control::error> {
  return control::detail::execute_request(hotkey.request, context.synthetic_source.view());
}

auto compile_fast_swipe_replay(
    gesture::direction swipe_direction,
    const cg::event_source &source) -> bool {
  if (!gesture::post_swipe(source.view(), swipe_direction)) {
    os_log_error(diagnostics::navigation_log(),
                 "Fast swipe replay failed (direction=%{public}s)",
                 gesture_direction_name(swipe_direction).data());
    return false;
  }

  return true;
}

auto initialize_runtime(
    const detail::runtime_config &config,
    runtime_context &context,
    cf::retained<CFRunLoopSourceRef> &run_loop_source,
    cg::event_tap &tap) -> bool {
  context.config = config;
  context.synthetic_source =
      cg::event_source::create(kCGEventSourceStateHIDSystemState);
  if (!context.synthetic_source) {
    os_log_error(diagnostics::lifecycle_log(),
                 "Runtime initialization failed (code=event-source-unavailable)");
    return false;
  }

  context.synthetic_source.set_user_data(detail::synthetic_event_marker);

  const auto event_mask =
      CGEventMaskBit(cg::gesture_event_type) | CGEventMaskBit(kCGEventKeyDown);
  tap = cg::event_tap::create(
      kCGHIDEventTap,
      kCGHeadInsertEventTap,
      kCGEventTapOptionDefault,
      event_mask,
      [](CGEventTapProxy /*proxy*/, CGEventType type, CGEventRef event_ref, void *user_info)
          -> CGEventRef {
            auto *context = static_cast<runtime_context *>(user_info);
            const auto event = cg::event_view{event_ref};

            if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
              if (type == kCGEventTapDisabledByTimeout) {
                os_log_error(diagnostics::lifecycle_log(),
                             "Event tap disabled by timeout; requesting re-enable");
              } else {
                os_log_error(diagnostics::lifecycle_log(),
                             "Event tap disabled by user input; requesting re-enable");
              }
              if (context != nullptr && context->tap) {
                context->tap.enable(true);
              }
              return event_ref;
            }

            if (context == nullptr) {
              return event_ref;
            }

            if (type == cg::gesture_event_type) {
              if (!context->config.fast_swipe || detail::is_synthetic_daemon_event(event)) {
                return event_ref;
              }

              gesture::phase phase{};
              gesture::direction swipe_direction{};
              if (!detail::decode_swipe_phase(event, phase) ||
                  !detail::decode_swipe_direction(event, swipe_direction)) {
                return event_ref;
              }

              if (phase == gesture::phase::begin) {
                os_log_debug(diagnostics::navigation_log(),
                             "Fast swipe recognized (direction=%{public}s)",
                             gesture_direction_name(swipe_direction).data());
                if (is_horizontal_direction(swipe_direction)) {
                  space_bounds bounds{};
                  if (active_display_space_bounds(bounds)) {
                    if (swipe_direction == gesture::direction::left && bounds.current_index == 0) {
                      os_log_debug(diagnostics::navigation_log(),
                                   "Fast swipe suppressed at workspace boundary (direction=left)");
                      return nullptr;
                    }

                    if (swipe_direction == gesture::direction::right &&
                        bounds.current_index >= bounds.num_spaces - 1) {
                      os_log_debug(diagnostics::navigation_log(),
                                   "Fast swipe suppressed at workspace boundary (direction=right)");
                      return nullptr;
                    }
                  }
                }

                if (!compile_fast_swipe_replay(swipe_direction, context->synthetic_source)) {
                  return event_ref;
                }
                os_log_debug(diagnostics::navigation_log(),
                             "Fast swipe replay completed (direction=%{public}s)",
                             gesture_direction_name(swipe_direction).data());
              }

              return nullptr;
            }

            if (type != kCGEventKeyDown) {
              return event_ref;
            }

            // While a shortcut is being recorded, let keystrokes reach the editor
            // instead of firing a matching hotkey. Gestures (fast swipe, above)
            // stay live so swiping keeps working during recording.
            if (context->input_suspended.load(std::memory_order_relaxed)) {
              return event_ref;
            }

            for (const auto &hotkey : context->config.active_hotkeys) {
              if (!hotkey_matches(hotkey, event)) {
                continue;
              }

              (void)execute_hotkey(*context, hotkey);
              return nullptr;
            }

            return event_ref;
          },
      &context);
  if (!tap) {
    os_log_error(diagnostics::lifecycle_log(),
                 "Runtime initialization failed (code=event-tap-unavailable)");
    return false;
  }

  context.tap = tap.view();
  run_loop_source = tap.create_run_loop_source();
  if (!run_loop_source) {
    os_log_error(diagnostics::lifecycle_log(),
                 "Runtime initialization failed (code=run-loop-source-unavailable)");
    return false;
  }

  return true;
}

}  // namespace

class runtime::impl final {
 public:
  options options{};
  std::filesystem::path settings_path;
  runtime_context context{};
  cg::event_tap tap{};
  cf::retained<CFRunLoopSourceRef> run_loop_source{};
  cf::view<CFRunLoopRef> run_loop{};
  ns::workspace workspace{};
  ns::notification_center workspace_notification_center{};
  ns::notification_observer active_space_observer{};
  std::optional<workspace_state> last_workspace_state{};

  static void handle_active_space_change(void *context) {
    auto *self = static_cast<impl *>(context);
    if (self == nullptr) {
      return;
    }

    self->notify_active_space_change(false);
  }

  auto apply_settings_document(const settings::document &document) -> void {
    context.config = detail::compile_runtime_config(document);
    os_log_info(diagnostics::settings_log(),
                "Runtime settings compiled (active-hotkeys=%{public}lu inert-actions=%{public}lu)",
                static_cast<unsigned long>(context.config.active_hotkeys.size()),
                static_cast<unsigned long>(context.config.inert_action_ids.size()));
    log_inert_actions(context.config);
  }

  auto register_active_space_observer() -> bool {
    if (options.observer.active_space_changed == nullptr) {
      return true;
    }

    workspace = ns::workspace::shared();
    if (!workspace) {
      os_log_error(diagnostics::lifecycle_log(),
                   "Active Space observer setup failed (code=workspace-unavailable)");
      return false;
    }

    workspace_notification_center = workspace.notification_center();
    if (!workspace_notification_center) {
      os_log_error(diagnostics::lifecycle_log(),
                   "Active Space observer setup failed (code=notification-center-unavailable)");
      return false;
    }

    active_space_observer = workspace_notification_center.add_observer(
        ns::active_space_did_change_notification(), &impl::handle_active_space_change, this);
    if (!active_space_observer) {
      os_log_error(diagnostics::lifecycle_log(),
                   "Active Space observer setup failed (code=observer-registration-failed)");
      return false;
    }

    return true;
  }

  auto notify_active_space_change(bool force) -> void {
    if (options.observer.active_space_changed == nullptr) {
      return;
    }

    const auto state = read_workspace_state();
    if (!state) {
      os_log_debug(diagnostics::navigation_log(),
                   "Active workspace state unavailable during change notification");
      return;
    }

    if (!force && last_workspace_state.has_value() && *last_workspace_state == *state) {
      return;
    }

    last_workspace_state = *state;
    options.observer.active_space_changed(*state, options.observer.context);
  }
};

namespace detail {

auto resolve_settings_path(const daemon::options &options)
    -> std::expected<std::filesystem::path, settings::error> {
  if (options.settings_path_override.has_value()) {
    return *options.settings_path_override;
  }

  return settings::default_path();
}

auto compile_runtime_config(const settings::document &document) -> runtime_config {
  runtime_config config;
  config.fast_swipe = document.fast_swipe;

  for (const auto &[action_id, hotkey] : document.hotkeys) {
    if (!hotkey.has_value() || !hotkey->enabled) {
      continue;
    }

    const auto compiled = compile_action(action_id, *hotkey, document);
    if (!compiled.has_value()) {
      config.inert_action_ids.emplace_back(action_id);
      continue;
    }

    config.active_hotkeys.push_back(*compiled);
  }

  std::ranges::sort(
      config.active_hotkeys,
      [](const compiled_hotkey &lhs, const compiled_hotkey &rhs) {
        return lhs.action_id < rhs.action_id;
      });
  std::ranges::sort(config.inert_action_ids);
  config.inert_action_ids.erase(
      std::unique(config.inert_action_ids.begin(), config.inert_action_ids.end()),
      config.inert_action_ids.end());

  return config;
}
auto decode_swipe_direction(cg::event_view event, gesture::direction &out_direction) noexcept
    -> bool {
  const auto event_gesture_type = event.integer_field(cg::gesture_type_field);
  const auto delta = event.double_field(cg::gesture_delta_field);

  switch (event_gesture_type) {
    case 1:
      out_direction = (delta > 0.0) ? gesture::direction::right : gesture::direction::left;
      return true;
    case 2:
      out_direction = (delta > 0.0) ? gesture::direction::up : gesture::direction::down;
      return true;
    default:
      return false;
  }
}

auto decode_swipe_phase(cg::event_view event, gesture::phase &out_phase) noexcept -> bool {
  switch (event.integer_field(cg::gesture_phase_field)) {
    case static_cast<std::int64_t>(gesture::phase::begin):
      out_phase = gesture::phase::begin;
      return true;
    case static_cast<std::int64_t>(gesture::phase::update):
      out_phase = gesture::phase::update;
      return true;
    case static_cast<std::int64_t>(gesture::phase::end):
      out_phase = gesture::phase::end;
      return true;
    default:
      return false;
  }
}

auto is_synthetic_daemon_event(cg::event_view event) noexcept -> bool {
  return event.integer_field(kCGEventSourceUserData) == synthetic_event_marker;
}

}  // namespace detail

runtime::runtime() noexcept = default;

runtime::runtime(runtime &&other) noexcept = default;

auto runtime::operator=(runtime &&other) noexcept -> runtime & = default;

runtime::~runtime() {
  stop();
}

auto runtime::start(const options &options) -> std::expected<void, error> {
  if (impl_) {
    return std::unexpected(already_running_error());
  }

  const auto settings_path = detail::resolve_settings_path(options);
  if (!settings_path) {
    return std::unexpected(settings_error(settings_path.error().message));
  }

  const auto settings_document = settings::load(*settings_path);
  if (!settings_document) {
    return std::unexpected(settings_error(settings_document.error().message));
  }

  if (!ax::is_process_trusted()) {
    return std::unexpected(permission_error(
        "Accessibility permission is required. Enable it in System Settings > Privacy & "
        "Security > Accessibility and try again."));
  }

  auto started = std::make_unique<impl>();
  started->options = options;
  started->settings_path = *settings_path;
  started->apply_settings_document(*settings_document);
  if (!initialize_runtime(
          started->context.config, started->context, started->run_loop_source, started->tap)) {
    return std::unexpected(
        runtime_error("Failed to initialize the SpaceRabbit daemon runtime."));
  }

  if (!started->register_active_space_observer()) {
    started->tap.enable(false);
    return std::unexpected(
        runtime_error("Failed to register the active Space observer."));
  }

  started->run_loop = cf::current_run_loop();
  cf::add_source(
      started->run_loop,
      cf::view<CFRunLoopSourceRef>{started->run_loop_source.get()},
      cf::string_view{kCFRunLoopCommonModes});
  started->tap.enable(true);
  started->notify_active_space_change(true);

  impl_ = std::move(started);
  return {};
}

void runtime::stop() noexcept {
  if (!impl_) {
    return;
  }

  impl_->tap.enable(false);
  impl_->active_space_observer = {};
  impl_->workspace_notification_center = {};
  impl_->workspace = {};

  if (impl_->run_loop && impl_->run_loop_source) {
    CFRunLoopRemoveSource(
        impl_->run_loop.get(), impl_->run_loop_source.get(), kCFRunLoopCommonModes);
    CFRunLoopWakeUp(impl_->run_loop.get());
  }

  impl_.reset();
}

auto runtime::reload_settings() -> std::expected<void, error> {
  if (!impl_) {
    return std::unexpected(not_running_error());
  }

  const auto settings_document = settings::load(impl_->settings_path);
  if (!settings_document) {
    return std::unexpected(settings_error(settings_document.error().message));
  }

  impl_->apply_settings_document(*settings_document);
  impl_->notify_active_space_change(true);
  return {};
}

auto runtime::current_workspace_state() const -> std::expected<workspace_state, error> {
  if (!impl_) {
    return std::unexpected(not_running_error());
  }

  return read_workspace_state();
}

void runtime::set_input_suspended(bool suspended) noexcept {
  if (!impl_) {
    return;
  }

  impl_->context.input_suspended.store(suspended, std::memory_order_relaxed);
}

auto runtime::running() const noexcept -> bool {
  return impl_ != nullptr;
}

}  // namespace spacerabbit::daemon
