#include <gtest/gtest.h>

#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <spacerabbit/settings.hpp>

namespace {

namespace fs = std::filesystem;
namespace settings = spacerabbit::settings;

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

  const auto path = fs::temp_directory_path() /
                    ("spacerabbit-settings-tests-" + std::to_string(++counter));
  fs::remove_all(path);
  fs::create_directories(path);
  return path;
}

void write_file(const fs::path &path, std::string_view contents) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  ASSERT_TRUE(output.is_open());
  output << contents;
  ASSERT_TRUE(output.good());
}

TEST(settings_tests, loads_document_with_legacy_telemetry_field) {
  const auto temp_dir = make_temp_dir();
  const auto path = temp_dir / "settings.json";
  write_file(
      path,
      R"json({
        "version": "1.0",
        "workspaceWrap": true,
        "displayWrap": false,
        "trayScroll": false,
        "trayScrollInverted": true,
        "hotkeys": {
          "switch_space_left": { "key": "h", "modifiers": ["option"], "enabled": true },
          "custom_action": { "key": "x", "modifiers": ["cmd", "shift"], "enabled": false }
        },
        "fastSwipe": false,
        "telemetryEnabled": false
      })json");

  const auto loaded = settings::load(path);
  ASSERT_TRUE(loaded.has_value()) << loaded.error().message;

  EXPECT_EQ(loaded->version, "1.0");
  EXPECT_TRUE(loaded->workspace_wrap);
  EXPECT_FALSE(loaded->display_wrap);
  EXPECT_FALSE(loaded->tray_scroll);
  EXPECT_TRUE(loaded->tray_scroll_inverted);
  EXPECT_FALSE(loaded->fast_swipe);
  ASSERT_TRUE(loaded->hotkeys.contains("switch_space_left"));
  ASSERT_TRUE(loaded->hotkeys.at("switch_space_left").has_value());
  EXPECT_EQ(loaded->hotkeys.at("switch_space_left")->key, "h");
  EXPECT_EQ(
      loaded->hotkeys.at("switch_space_left")->modifiers,
      std::vector<std::string>({"option"}));
  ASSERT_TRUE(loaded->hotkeys.contains("custom_action"));
  ASSERT_TRUE(loaded->hotkeys.at("custom_action").has_value());
  EXPECT_FALSE(loaded->hotkeys.at("custom_action")->enabled);
}

TEST(settings_tests, missing_file_returns_file_not_found_error) {
  const auto temp_dir = make_temp_dir();
  const auto missing_path = temp_dir / "missing.json";

  const auto loaded = settings::load(missing_path);
  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().code, settings::error_code::file_not_found);
  EXPECT_EQ(loaded.error().path, missing_path);
}

TEST(settings_tests, invalid_json_returns_error) {
  const auto temp_dir = make_temp_dir();
  const auto path = temp_dir / "settings.json";
  write_file(path, "{ this is not valid json");

  const auto loaded = settings::load(path);
  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().code, settings::error_code::invalid_json);
}

TEST(settings_tests, invalid_schema_returns_error) {
  const auto temp_dir = make_temp_dir();
  const auto path = temp_dir / "settings.json";
  write_file(
      path,
      R"json({
        "version": "1.0",
        "workspaceWrap": "true",
        "displayWrap": false,
        "hotkeys": {}
      })json");

  const auto loaded = settings::load(path);
  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().code, settings::error_code::invalid_schema);
}

TEST(settings_tests, unsupported_version_returns_error) {
  const auto temp_dir = make_temp_dir();
  const auto path = temp_dir / "settings.json";
  write_file(
      path,
      R"json({
        "version": "2.0",
        "workspaceWrap": false,
        "displayWrap": false,
        "hotkeys": {}
      })json");

  const auto loaded = settings::load(path);
  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().code, settings::error_code::unsupported_version);
}

TEST(settings_tests, missing_optional_booleans_use_compatibility_defaults) {
  const auto temp_dir = make_temp_dir();
  const auto path = temp_dir / "settings.json";
  write_file(
      path,
      R"json({
        "version": "1.0",
        "workspaceWrap": false,
        "displayWrap": true,
        "hotkeys": {}
      })json");

  const auto loaded = settings::load(path);
  ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
  EXPECT_TRUE(loaded->tray_scroll);
  EXPECT_FALSE(loaded->tray_scroll_inverted);
  EXPECT_TRUE(loaded->fast_swipe);
}

TEST(settings_tests, missing_known_hotkeys_are_backfilled_from_defaults) {
  const auto temp_dir = make_temp_dir();
  const auto path = temp_dir / "settings.json";
  write_file(
      path,
      R"json({
        "version": "1.0",
        "workspaceWrap": false,
        "displayWrap": false,
        "hotkeys": {}
      })json");

  const auto loaded = settings::load(path);
  ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
  ASSERT_TRUE(loaded->hotkeys.contains("switch_space_left"));
  ASSERT_TRUE(loaded->hotkeys.at("switch_space_left").has_value());
  EXPECT_EQ(loaded->hotkeys.at("switch_space_left")->key, "a");
  EXPECT_EQ(
      loaded->hotkeys.at("switch_space_left")->modifiers,
      std::vector<std::string>({"option"}));
  ASSERT_TRUE(loaded->hotkeys.contains("window_focus_next"));
  ASSERT_TRUE(loaded->hotkeys.at("window_focus_next").has_value());
  EXPECT_EQ(loaded->hotkeys.at("window_focus_next")->key, "Tab");
}

TEST(settings_tests, unknown_valid_hotkeys_are_preserved) {
  const auto temp_dir = make_temp_dir();
  const auto path = temp_dir / "settings.json";
  write_file(
      path,
      R"json({
        "version": "1.0",
        "workspaceWrap": false,
        "displayWrap": false,
        "hotkeys": {
          "future_action": { "key": "k", "modifiers": ["ctrl"], "enabled": true }
        }
      })json");

  const auto loaded = settings::load(path);
  ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
  ASSERT_TRUE(loaded->hotkeys.contains("future_action"));
  ASSERT_TRUE(loaded->hotkeys.at("future_action").has_value());
  EXPECT_EQ(loaded->hotkeys.at("future_action")->key, "k");
  EXPECT_EQ(
      loaded->hotkeys.at("future_action")->modifiers,
      std::vector<std::string>({"ctrl"}));
}

TEST(settings_tests, invalid_known_hotkey_falls_back_and_invalid_unknown_hotkey_is_dropped) {
  const auto temp_dir = make_temp_dir();
  const auto path = temp_dir / "settings.json";
  write_file(
      path,
      R"json({
        "version": "1.0",
        "workspaceWrap": false,
        "displayWrap": false,
        "hotkeys": {
          "switch_space_left": { "key": 1, "modifiers": ["option"], "enabled": true },
          "future_action": { "key": "k", "modifiers": [1], "enabled": true }
        }
      })json");

  const auto loaded = settings::load(path);
  ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
  ASSERT_TRUE(loaded->hotkeys.contains("switch_space_left"));
  ASSERT_TRUE(loaded->hotkeys.at("switch_space_left").has_value());
  EXPECT_EQ(loaded->hotkeys.at("switch_space_left")->key, "a");
  EXPECT_FALSE(loaded->hotkeys.contains("future_action"));
}

TEST(settings_tests, default_path_prefers_lowercase_xdg_directory) {
  const std::lock_guard<std::mutex> lock(env_lock);
  const auto temp_dir = make_temp_dir();
  const auto home_dir = temp_dir / "home";
  const auto xdg_dir = temp_dir / "xdg";
  fs::create_directories(home_dir);
  fs::create_directories(xdg_dir / "spacerabbit");
  fs::create_directories(xdg_dir / "SpaceRabbit");
  const scoped_env_var home("HOME", home_dir.string());
  const scoped_env_var xdg("XDG_CONFIG_HOME", xdg_dir.string());

  const auto path = settings::default_path();
  ASSERT_TRUE(path.has_value()) << path.error().message;
  EXPECT_EQ(*path, xdg_dir / "spacerabbit" / "settings.json");
}

TEST(settings_tests, default_path_uses_uppercase_xdg_directory_when_only_uppercase_exists) {
  const std::lock_guard<std::mutex> lock(env_lock);
  const auto temp_dir = make_temp_dir();
  const auto home_dir = temp_dir / "home";
  const auto xdg_dir = temp_dir / "xdg";
  fs::create_directories(home_dir);
  fs::create_directories(xdg_dir / "SpaceRabbit");
  const scoped_env_var home("HOME", home_dir.string());
  const scoped_env_var xdg("XDG_CONFIG_HOME", xdg_dir.string());

  const auto path = settings::default_path();
  ASSERT_TRUE(path.has_value()) << path.error().message;
  EXPECT_EQ(*path, xdg_dir / "SpaceRabbit" / "settings.json");
}

TEST(settings_tests, default_path_falls_back_to_macos_application_support) {
  const std::lock_guard<std::mutex> lock(env_lock);
  const auto temp_dir = make_temp_dir();
  const auto home_dir = temp_dir / "home";
  const auto xdg_dir = temp_dir / "xdg";
  fs::create_directories(home_dir);
  fs::create_directories(xdg_dir);
  const scoped_env_var home("HOME", home_dir.string());
  const scoped_env_var xdg("XDG_CONFIG_HOME", xdg_dir.string());

  const auto path = settings::default_path();
  ASSERT_TRUE(path.has_value()) << path.error().message;
  EXPECT_EQ(
      *path,
      home_dir / "Library" / "Application Support" / "SpaceRabbit" / "settings.json");
}

}  // namespace
