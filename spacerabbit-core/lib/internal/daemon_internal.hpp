#pragma once

#include <ApplicationServices/ApplicationServices.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include <spacerabbit/daemon.hpp>
#include <spacerabbit/cg.hpp>
#include <spacerabbit/control.hpp>
#include <spacerabbit/gesture.hpp>
#include <spacerabbit/settings.hpp>

namespace spacerabbit::daemon::detail {

inline constexpr std::int64_t synthetic_event_marker = 0x5352544150494E47LL;

struct compiled_hotkey final {
  std::string action_id;
  CGKeyCode key_code = 0;
  CGEventFlags modifier_flags = 0;
  control::request request{};

  [[nodiscard]] auto operator==(const compiled_hotkey &) const -> bool = default;
};

struct runtime_config final {
  bool fast_swipe = false;
  std::vector<compiled_hotkey> active_hotkeys;
  std::vector<std::string> inert_action_ids;

  [[nodiscard]] auto operator==(const runtime_config &) const -> bool = default;
};

[[nodiscard]] auto resolve_settings_path(const daemon::options &options)
    -> std::expected<std::filesystem::path, settings::error>;

[[nodiscard]] constexpr auto map_settings_error(settings::error_code) noexcept
    -> daemon::exit_code {
  return daemon::exit_code::settings_error;
}

[[nodiscard]] auto compile_runtime_config(const settings::document &document) -> runtime_config;

[[nodiscard]] auto decode_swipe_direction(cg::event_view event, gesture::direction &out_direction)
    noexcept -> bool;

[[nodiscard]] auto decode_swipe_phase(cg::event_view event, gesture::phase &out_phase) noexcept
    -> bool;

[[nodiscard]] auto is_synthetic_daemon_event(cg::event_view event) noexcept -> bool;

}  // namespace spacerabbit::daemon::detail
