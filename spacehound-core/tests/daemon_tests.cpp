#include <gtest/gtest.h>

#include <Carbon/Carbon.h>

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "internal/daemon_internal.hpp"

namespace {

namespace cg = spacehound::cg;
namespace control = spacehound::control;
namespace daemon = spacehound::daemon;
namespace detail = spacehound::daemon::detail;
namespace fs = std::filesystem;
namespace gesture = spacehound::gesture;
namespace settings = spacehound::settings;

std::mutex env_lock;

class scoped_env_var final {
 public:
  scoped_env_var(const char *name, const std::optional<std::string> &value)
      : name_(name), old_value_(read(name)) {
    if (value.has_value()) {
      if (setenv(name_, value->c_str(), 1) != 0) {
        std::abort();
      }
    } else if (unsetenv(name_) != 0) {
      std::abort();
    }
  }

  ~scoped_env_var() {
    if (old_value_.has_value()) {
      if (setenv(name_, old_value_->c_str(), 1) != 0) {
        std::abort();
      }
    } else if (unsetenv(name_) != 0) {
      std::abort();
    }
  }

  scoped_env_var(const scoped_env_var &) = delete;
  auto operator=(const scoped_env_var &) -> scoped_env_var & = delete;

 private:
  static auto read(const char *name) -> std::optional<std::string> {
    const char *value = std::getenv(name);
    if (value == nullptr) {
      return std::nullopt;
    }

    return std::string{value};
  }

  const char *name_;
  std::optional<std::string> old_value_;
};

auto make_temp_dir() -> fs::path {
  static std::uint64_t counter = 0;

  const auto path =
      fs::temp_directory_path() / ("spacehound-daemon-tests-" + std::to_string(++counter));
  fs::remove_all(path);
  fs::create_directories(path);
  return path;
}

TEST(daemon_tests, resolve_settings_path_prefers_override) {
  const fs::path expected = "/tmp/spacehoundd-settings.json";

  daemon::options options;
  options.settings_path_override = expected;

  const auto resolved = detail::resolve_settings_path(options);
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  EXPECT_EQ(*resolved, expected);
}

TEST(daemon_tests, resolve_settings_path_uses_default_path_when_override_is_missing) {
  std::lock_guard lock(env_lock);

  const auto temp_dir = make_temp_dir();
  const auto xdg_dir = temp_dir / "xdg";
  fs::create_directories(xdg_dir / "spacehound");

  scoped_env_var home("HOME", temp_dir.string());
  scoped_env_var xdg("XDG_CONFIG_HOME", xdg_dir.string());

  const auto resolved = detail::resolve_settings_path(daemon::options{});
  ASSERT_TRUE(resolved.has_value()) << resolved.error().message;
  EXPECT_EQ(*resolved, xdg_dir / "spacehound" / "settings.json");
}

TEST(daemon_tests, compile_runtime_config_maps_supported_requests_and_inert_actions) {
  settings::document document;
  document.fast_swipe = true;
  document.workspace_wrap = true;
  document.display_wrap = true;
  document.workspace_targets_focused_display = false;
  document.move_cursor_to_target_display = false;
  document.hotkeys["switch_space_left"] = settings::hotkey_setting{
      .key = "h",
      .modifiers = {"option"},
      .enabled = true,
  };
  document.hotkeys["switch_space_3"] = settings::hotkey_setting{
      .key = "3",
      .modifiers = {"option"},
      .enabled = true,
  };
  document.hotkeys["switch_display_left"] = settings::hotkey_setting{
      .key = "a",
      .modifiers = {"option", "ctrl"},
      .enabled = true,
  };
  document.hotkeys["mission_control_toggle"] = settings::hotkey_setting{
      .key = "w",
      .modifiers = {"option"},
      .enabled = true,
  };
  document.hotkeys["switch_space_right"] = settings::hotkey_setting{
      .key = "right",
      .modifiers = {"hyper"},
      .enabled = true,
  };
  document.hotkeys["window_focus_next"] = settings::hotkey_setting{
      .key = "tab",
      .modifiers = {"option"},
      .enabled = true,
  };
  document.hotkeys["window_focus_prev"] = settings::hotkey_setting{
      .key = "tab",
      .modifiers = {"option", "shift"},
      .enabled = true,
  };

  const auto config = detail::compile_runtime_config(document);

  EXPECT_TRUE(config.fast_swipe);
  ASSERT_EQ(config.active_hotkeys.size(), 6U);

  std::vector<control::request> requests;
  requests.reserve(config.active_hotkeys.size());
  for (const auto &hotkey : config.active_hotkeys) {
    requests.push_back(hotkey.request);
  }

  EXPECT_EQ(
      requests,
      (std::vector<control::request>{
          control::system_ui_request{
              .element = control::system_ui_element::mission_control,
          },
          control::display_request{
              .action = control::display_action::left,
              .wrap = true,
              .move_cursor_to_target_display = false,
          },
          control::workspace_request{
              .action = control::workspace_action::go_to,
              .index = 3,
              .wrap = true,
              .target_focused_display = false,
          },
          control::workspace_request{
              .action = control::workspace_action::left,
              .wrap = true,
              .target_focused_display = false,
          },
          control::window_focus_request{
              .direction = control::window_focus_direction::next,
          },
          control::window_focus_request{
              .direction = control::window_focus_direction::previous,
          },
      }));
  EXPECT_EQ(config.inert_action_ids, std::vector<std::string>({"switch_space_right"}));
}

TEST(daemon_tests, synthetic_marker_and_swipe_decoders_match_generated_swipe_events) {
  const auto event = cg::event::adopt(CGEventCreate(nullptr));
  ASSERT_TRUE(event);
  gesture::populate_swipe_event(
      event.view(), gesture::phase::update, gesture::direction::down);
  event.set_integer_field(kCGEventSourceUserData, detail::synthetic_event_marker);
  EXPECT_TRUE(detail::is_synthetic_daemon_event(event.view()));

  gesture::direction direction{};
  ASSERT_TRUE(detail::decode_swipe_direction(event.view(), direction));
  EXPECT_EQ(direction, gesture::direction::down);

  gesture::phase phase{};
  ASSERT_TRUE(detail::decode_swipe_phase(event.view(), phase));
  EXPECT_EQ(phase, gesture::phase::update);

  const auto plain_event = cg::event::adopt(CGEventCreate(nullptr));
  ASSERT_TRUE(plain_event);
  gesture::populate_swipe_event(
      plain_event.view(), gesture::phase::begin, gesture::direction::left);
  EXPECT_FALSE(detail::is_synthetic_daemon_event(plain_event.view()));
}

TEST(daemon_tests, runtime_reports_not_running_before_start) {
  daemon::runtime runtime;

  EXPECT_FALSE(runtime.running());

  const auto workspace_state = runtime.current_workspace_state();
  ASSERT_FALSE(workspace_state.has_value());
  EXPECT_EQ(workspace_state.error().code, daemon::error_code::not_running);

  const auto reload_result = runtime.reload_settings();
  ASSERT_FALSE(reload_result.has_value());
  EXPECT_EQ(reload_result.error().code, daemon::error_code::not_running);

  runtime.stop();
  EXPECT_FALSE(runtime.running());
}

}  // namespace
