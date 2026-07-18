#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <unistd.h>

#include <spacerabbit/ax.hpp>
#include <spacerabbit/cgs.hpp>
#include <spacerabbit/cf.hpp>
#include <spacerabbit/cg.hpp>
#include <spacerabbit/control.hpp>
#include <spacerabbit/dispatch.hpp>
#include <spacerabbit/gesture.hpp>
#include <spacerabbit/ns.hpp>
#include <spacerabbit/settings.hpp>
#include <spacerabbit/version.hpp>

int main() {
  // This binary exercises the installed package surface the way a downstream user would.
  if (spacerabbit::version_string().empty()) {
    std::cerr << "version string is empty\n";
    return 1;
  }

  const auto settings_file = std::filesystem::temp_directory_path() /
                             ("spacerabbit-package-consumer-settings-" +
                              std::to_string(static_cast<long long>(getpid())) + ".json");
  {
    std::ofstream output(settings_file, std::ios::binary);
    if (!output) {
      std::cerr << "failed to open temporary settings file\n";
      return 1;
    }

    output << R"json({
      "version": "1.0",
      "workspaceWrap": false,
      "displayWrap": false,
      "hotkeys": {}
    })json";
    if (!output.good()) {
      std::cerr << "failed to write temporary settings file\n";
      return 1;
    }
  }

  const auto loaded_settings = spacerabbit::settings::load(settings_file);
  std::filesystem::remove(settings_file);
  if (!loaded_settings) {
    std::cerr << "failed to load settings: " << loaded_settings.error().message << '\n';
    return 1;
  }
  if (loaded_settings->version != "1.0" ||
      !loaded_settings->hotkeys.contains("switch_space_left")) {
    std::cerr << "settings loader returned unexpected values\n";
    return 1;
  }

  const auto retained_true =
      spacerabbit::cf::retain(spacerabbit::cf::view{kCFBooleanTrue});
  if (!retained_true) {
    std::cerr << "failed to retain boolean constant\n";
    return 1;
  }

  // Constructing these wrappers verifies the package exports the Core Graphics and AX helpers.
  const auto source =
      spacerabbit::cg::event_source::create(kCGEventSourceStateHIDSystemState);
  if (!source) {
    std::cerr << "failed to create event source\n";
    return 1;
  }

  const auto app = spacerabbit::ax::ui_element::create_application(getpid());
  if (!app) {
    std::cerr << "failed to create AX application element\n";
    return 1;
  }

  const auto frame = spacerabbit::ax::value::create(CGRectMake(1.0, 2.0, 3.0, 4.0));
  if (!frame) {
    std::cerr << "failed to create AX value\n";
    return 1;
  }

  const spacerabbit::cf::dictionary_entry trust_entries[] = {{
      .key = spacerabbit::ax::trusted_check_option_prompt,
      .value = spacerabbit::cf::type_view{kCFBooleanFalse},
  }};
  const auto trust_options = spacerabbit::cf::dictionary::create(trust_entries);
  if (!trust_options) {
    std::cerr << "failed to create trust dictionary\n";
    return 1;
  }

  (void)source.user_data();
  (void)app.view();
  (void)frame.type();
  (void)spacerabbit::cf::current_run_loop();
  (void)spacerabbit::ax::is_process_trusted();
  (void)spacerabbit::ax::is_process_trusted_with_options(trust_options.view());
  std::vector<CGDirectDisplayID> displays;
  const auto create_event = &spacerabbit::cg::event::create;
  const auto create_mouse_event = &spacerabbit::cg::event::create_mouse;
  const auto create_keyboard_event = &spacerabbit::cg::event::create_keyboard;
  const auto active_displays = &spacerabbit::cg::active_displays;
  const auto display_uuid_string = &spacerabbit::cg::display_uuid_string;
  const auto execute_control = &spacerabbit::control::execute;
  const spacerabbit::gesture::swipe_options swipe_options{};
  const auto swipe_direction = spacerabbit::gesture::direction::left;
  const auto swipe_phase = spacerabbit::gesture::phase::begin;
  const auto populate_swipe_event = &spacerabbit::gesture::populate_swipe_event;
  const auto create_swipe_event = &spacerabbit::gesture::create_swipe_event;
  const auto post_swipe = static_cast<bool (*)(
      spacerabbit::cg::event_source_view,
      spacerabbit::gesture::direction,
      CGEventTapLocation,
      const spacerabbit::gesture::swipe_options &) noexcept>(
      &spacerabbit::gesture::post_swipe);
  const auto post_tap_swipe = static_cast<bool (*)(
      CGEventTapProxy,
      spacerabbit::cg::event_source_view,
      spacerabbit::gesture::direction,
      const spacerabbit::gesture::swipe_options &) noexcept>(
      &spacerabbit::gesture::post_swipe);
  const auto workspace_shared = &spacerabbit::ns::workspace::shared;
  const auto active_space_notification =
      &spacerabbit::ns::active_space_did_change_notification;
  const auto dispatch_to_main = &spacerabbit::dispatch::to_main;
  const auto main_connection_id = &spacerabbit::cgs::main_connection_id;
  const auto copy_active_display =
      &spacerabbit::cgs::copy_active_menu_bar_display_identifier;

  spacerabbit::ns::running_application running_application;
  spacerabbit::ns::workspace workspace;
  spacerabbit::ns::notification_center notification_center;
  spacerabbit::ns::notification_observer notification_observer;
  spacerabbit::ns::screen screen;
  const spacerabbit::control::workspace_request workspace_request{
      .action = spacerabbit::control::workspace_action::left,
  };
  const spacerabbit::control::display_request display_request{
      .action = spacerabbit::control::display_action::right,
      .index = 2,
  };
  const spacerabbit::control::system_ui_request system_ui_request{
      .element = spacerabbit::control::system_ui_element::mission_control,
  };

  (void)displays;
  (void)active_displays;
  (void)display_uuid_string;
  (void)execute_control;
  (void)swipe_options;
  (void)swipe_direction;
  (void)swipe_phase;
  (void)populate_swipe_event;
  (void)create_swipe_event;
  (void)post_swipe;
  (void)post_tap_swipe;
  (void)workspace_shared;
  (void)active_space_notification;
  (void)dispatch_to_main;
  (void)main_connection_id;
  (void)copy_active_display;
  (void)running_application;
  (void)workspace;
  (void)notification_center;
  (void)notification_observer;
  (void)screen;
  (void)workspace_request;
  (void)display_request;
  (void)system_ui_request;
  (void)create_event;
  (void)create_mouse_event;
  (void)create_keyboard_event;

  return 0;
}
