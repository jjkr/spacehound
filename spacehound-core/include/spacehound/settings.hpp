#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace spacehound::settings {

struct hotkey_setting final {
  std::string key;
  std::vector<std::string> modifiers;
  bool enabled = false;

  [[nodiscard]] auto operator==(const hotkey_setting &) const -> bool = default;
};

using hotkey_map = std::unordered_map<std::string, std::optional<hotkey_setting>>;

struct document final {
  std::string version;
  bool workspace_wrap = false;
  bool display_wrap = false;
  bool tray_scroll = true;
  bool tray_scroll_inverted = false;
  hotkey_map hotkeys;
  bool fast_swipe = true;
  bool move_cursor_to_active_display = true;

  [[nodiscard]] auto operator==(const document &) const -> bool = default;
};

enum class error_code {
  file_not_found,
  io_error,
  invalid_json,
  invalid_schema,
  unsupported_version,
};

struct error final {
  error_code code = error_code::io_error;
  std::string message;
  std::filesystem::path path;

  [[nodiscard]] auto operator==(const error &) const -> bool = default;
};

[[nodiscard]] auto load(const std::filesystem::path &path)
    -> std::expected<document, error>;

[[nodiscard]] auto default_path()
    -> std::expected<std::filesystem::path, error>;

}  // namespace spacehound::settings
