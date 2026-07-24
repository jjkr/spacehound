#include <spacehound/settings.hpp>

#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace spacehound::settings {
namespace {

using json = nlohmann::json;

constexpr std::string_view settings_file_name = "settings.json";

struct known_hotkey_default final {
  std::string_view action_id;
  std::string_view key;
  std::initializer_list<std::string_view> modifiers;
  bool enabled;
};

constexpr known_hotkey_default known_hotkey_defaults[] = {
    {"switch_space_left", "a", {"option"}, true},
    {"switch_space_right", "d", {"option"}, true},
    {"switch_space_1", "1", {"option"}, true},
    {"switch_space_2", "2", {"option"}, true},
    {"switch_space_3", "3", {"option"}, true},
    {"switch_space_4", "4", {"option"}, true},
    {"switch_space_5", "5", {"option"}, true},
    {"switch_space_6", "6", {"option"}, true},
    {"switch_space_7", "7", {"option"}, true},
    {"switch_space_8", "8", {"option"}, true},
    {"switch_space_9", "9", {"option"}, true},
    {"switch_space_10", "0", {"option"}, true},
    {"switch_display_left", "a", {"option", "ctrl"}, true},
    {"switch_display_right", "d", {"option", "ctrl"}, true},
    {"switch_display_1", "1", {"option", "ctrl"}, true},
    {"switch_display_2", "2", {"option", "ctrl"}, true},
    {"switch_display_3", "3", {"option", "ctrl"}, true},
    {"switch_display_4", "4", {"option", "ctrl"}, true},
    {"switch_display_5", "5", {"option", "ctrl"}, true},
    {"switch_display_6", "6", {"option", "ctrl"}, true},
    {"switch_display_7", "7", {"option", "ctrl"}, true},
    {"switch_display_8", "8", {"option", "ctrl"}, true},
    {"switch_display_9", "9", {"option", "ctrl"}, true},
    {"switch_display_10", "0", {"option", "ctrl"}, true},
    {"window_focus_next", "Tab", {"option"}, true},
    {"window_focus_prev", "Tab", {"option", "shift"}, true},
    {"mission_control_toggle", "w", {"option"}, true},
    {"expose_toggle", "e", {"option"}, true},
};

auto make_error(error_code code, const std::filesystem::path &path, std::string message)
    -> error {
  return error{
      .code = code,
      .message = std::move(message),
      .path = path,
  };
}

auto read_file(const std::filesystem::path &path) -> std::expected<std::string, error> {
  std::error_code filesystem_error;
  const bool exists = std::filesystem::exists(path, filesystem_error);
  if (filesystem_error) {
    return std::unexpected(make_error(
        error_code::io_error,
        path,
        "failed to check whether settings file exists: " + filesystem_error.message()));
  }

  if (!exists) {
    return std::unexpected(make_error(
        error_code::file_not_found,
        path,
        "settings file does not exist"));
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::unexpected(make_error(
        error_code::io_error,
        path,
        "failed to open settings file for reading"));
  }

  return std::string(
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{});
}

auto make_default_hotkey(const known_hotkey_default &value) -> std::optional<hotkey_setting> {
  hotkey_setting result;
  result.key = std::string{value.key};
  result.modifiers.reserve(value.modifiers.size());
  for (const auto modifier : value.modifiers) {
    result.modifiers.emplace_back(modifier);
  }
  result.enabled = value.enabled;
  return result;
}

auto parse_hotkey(const json &value) -> std::optional<std::optional<hotkey_setting>> {
  if (value.is_null()) {
    return std::optional<hotkey_setting>{};
  }

  if (!value.is_object()) {
    return std::nullopt;
  }

  const auto key_it = value.find("key");
  const auto modifiers_it = value.find("modifiers");
  const auto enabled_it = value.find("enabled");
  if (key_it == value.end() || modifiers_it == value.end() || enabled_it == value.end()) {
    return std::nullopt;
  }

  if (!key_it->is_string() || !modifiers_it->is_array() || !enabled_it->is_boolean()) {
    return std::nullopt;
  }

  hotkey_setting result;
  result.key = key_it->get<std::string>();
  if (result.key.empty()) {
    return std::nullopt;
  }

  result.modifiers.reserve(modifiers_it->size());
  for (const auto &modifier : *modifiers_it) {
    if (!modifier.is_string()) {
      return std::nullopt;
    }
    result.modifiers.push_back(modifier.get<std::string>());
  }

  result.enabled = enabled_it->get<bool>();
  return result;
}

auto parse_required_bool(
    const json &root,
    std::string_view key,
    bool &value,
    const std::filesystem::path &path) -> std::expected<void, error> {
  const auto it = root.find(key);
  if (it == root.end() || !it->is_boolean()) {
    return std::unexpected(make_error(
        error_code::invalid_schema,
        path,
        "missing or invalid boolean field: " + std::string{key}));
  }

  value = it->get<bool>();
  return {};
}

auto parse_optional_bool(
    const json &root,
    std::string_view key,
    bool default_value,
    bool &value,
    const std::filesystem::path &path) -> std::expected<void, error> {
  const auto it = root.find(key);
  if (it == root.end()) {
    value = default_value;
    return {};
  }

  if (!it->is_boolean()) {
    return std::unexpected(make_error(
        error_code::invalid_schema,
        path,
        "invalid boolean field: " + std::string{key}));
  }

  value = it->get<bool>();
  return {};
}

auto parse_supported_version(
    const json &root,
    const std::filesystem::path &path) -> std::expected<std::string, error> {
  const auto it = root.find("version");
  if (it == root.end() || !it->is_string()) {
    return std::unexpected(make_error(
        error_code::invalid_schema,
        path,
        "missing or invalid string field: version"));
  }

  const std::string version = it->get<std::string>();
  if (version.empty()) {
    return std::unexpected(make_error(
        error_code::invalid_schema,
        path,
        "version must be a non-empty string"));
  }

  const auto separator = version.find('.');
  const auto major_part = std::string_view{version}.substr(0, separator);
  if (major_part.empty()) {
    return std::unexpected(make_error(
        error_code::invalid_schema,
        path,
        "version must start with a major number"));
  }

  unsigned long major = 0;
  const auto *first = major_part.data();
  const auto *last = major_part.data() + major_part.size();
  const auto [ptr, parse_error] = std::from_chars(first, last, major);
  if (parse_error != std::errc{} || ptr != last) {
    return std::unexpected(make_error(
        error_code::invalid_schema,
        path,
        "version must start with a numeric major version"));
  }

  if (major != 1UL) {
    return std::unexpected(make_error(
        error_code::unsupported_version,
        path,
        "unsupported settings version: " + version));
  }

  return version;
}

auto parse_hotkeys(const json &root, const std::filesystem::path &path)
    -> std::expected<hotkey_map, error> {
  const auto it = root.find("hotkeys");
  if (it == root.end() || !it->is_object()) {
    return std::unexpected(make_error(
        error_code::invalid_schema,
        path,
        "missing or invalid object field: hotkeys"));
  }

  hotkey_map result;
  result.reserve(it->size() + std::size(known_hotkey_defaults));

  for (const auto &[action_id, value] : it->items()) {
    const auto parsed = parse_hotkey(value);
    if (!parsed.has_value()) {
      continue;
    }

    result.emplace(action_id, std::move(*parsed));
  }

  for (const auto &hotkey : known_hotkey_defaults) {
    const auto source_it = it->find(hotkey.action_id);
    if (source_it == it->end()) {
      result[std::string{hotkey.action_id}] = make_default_hotkey(hotkey);
      continue;
    }

    const auto parsed = parse_hotkey(*source_it);
    if (!parsed.has_value()) {
      result[std::string{hotkey.action_id}] = make_default_hotkey(hotkey);
      continue;
    }

    result[std::string{hotkey.action_id}] = std::move(*parsed);
  }

  return result;
}

auto parse_document(const json &root, const std::filesystem::path &path)
    -> std::expected<document, error> {
  if (!root.is_object()) {
    return std::unexpected(make_error(
        error_code::invalid_schema,
        path,
        "settings document must be a JSON object"));
  }

  document result;

  auto version = parse_supported_version(root, path);
  if (!version) {
    return std::unexpected(version.error());
  }
  result.version = std::move(*version);

  if (auto status = parse_required_bool(root, "workspaceWrap", result.workspace_wrap, path);
      !status) {
    return std::unexpected(status.error());
  }
  if (auto status = parse_required_bool(root, "displayWrap", result.display_wrap, path);
      !status) {
    return std::unexpected(status.error());
  }
  if (auto status = parse_optional_bool(root, "trayScroll", true, result.tray_scroll, path);
      !status) {
    return std::unexpected(status.error());
  }
  if (auto status =
          parse_optional_bool(root, "trayScrollInverted", false, result.tray_scroll_inverted, path);
      !status) {
    return std::unexpected(status.error());
  }
  if (auto hotkeys = parse_hotkeys(root, path); !hotkeys) {
    return std::unexpected(hotkeys.error());
  } else {
    result.hotkeys = std::move(*hotkeys);
  }
  if (auto status = parse_optional_bool(root, "fastSwipe", true, result.fast_swipe, path);
      !status) {
    return std::unexpected(status.error());
  }
  return result;
}

auto home_directory() -> std::optional<std::filesystem::path> {
  if (const char *home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    return std::filesystem::path{home};
  }

  if (const passwd *entry = getpwuid(getuid()); entry != nullptr && entry->pw_dir != nullptr &&
                                                entry->pw_dir[0] != '\0') {
    return std::filesystem::path{entry->pw_dir};
  }

  return std::nullopt;
}

auto resolve_xdg_base() -> std::optional<std::filesystem::path> {
  if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
    return std::filesystem::path{xdg};
  }

  const auto home = home_directory();
  if (!home) {
    return std::nullopt;
  }

  return *home / ".config";
}

}  // namespace

auto load(const std::filesystem::path &path) -> std::expected<document, error> {
  auto file_contents = read_file(path);
  if (!file_contents) {
    return std::unexpected(file_contents.error());
  }

  json parsed;
  try {
    parsed = json::parse(*file_contents);
  } catch (const json::parse_error &parse_error) {
    return std::unexpected(make_error(
        error_code::invalid_json,
        path,
        "failed to parse settings JSON: " + std::string{parse_error.what()}));
  }

  return parse_document(parsed, path);
}

auto default_path() -> std::expected<std::filesystem::path, error> {
  if (const auto xdg_base = resolve_xdg_base()) {
    std::error_code directory_error;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    auto iterator = std::filesystem::directory_iterator(*xdg_base, options, directory_error);
    if (!directory_error) {
      for (const auto &entry : iterator) {
        std::error_code entry_error;
        if (entry.path().filename() == "spacehound" && entry.is_directory(entry_error) &&
            !entry_error) {
          return entry.path() / settings_file_name;
        }
      }

      iterator = std::filesystem::directory_iterator(*xdg_base, options, directory_error);
      if (!directory_error) {
        for (const auto &entry : iterator) {
          std::error_code entry_error;
          if (entry.path().filename() == "SpaceHound" && entry.is_directory(entry_error) &&
              !entry_error) {
            return entry.path() / settings_file_name;
          }
        }
      }
    }
  }

  const auto home = home_directory();
  if (!home) {
    return std::unexpected(make_error(
        error_code::io_error,
        {},
        "failed to resolve the current user's home directory"));
  }

  return *home / "Library" / "Application Support" / "SpaceHound" / settings_file_name;
}

}  // namespace spacehound::settings
