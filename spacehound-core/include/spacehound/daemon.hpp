#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <spacehound/control.hpp>

namespace spacehound::daemon {

enum class error_code {
  settings_error,
  permission_denied,
  state_unavailable,
  runtime_error,
  already_running,
  not_running,
};

struct error final {
  error_code code = error_code::runtime_error;
  std::string message;

  [[nodiscard]] auto operator==(const error &) const -> bool = default;
};

struct workspace_state final {
  std::size_t current_space = 0;
  std::size_t num_spaces = 0;

  [[nodiscard]] auto operator==(const workspace_state &) const -> bool = default;
};

using active_space_change_callback = void (*)(const workspace_state &, void *);

struct observer final {
  active_space_change_callback active_space_changed = nullptr;
  void *context = nullptr;

  [[nodiscard]] auto operator==(const observer &) const -> bool = default;
};

struct options final {
  std::optional<std::filesystem::path> settings_path_override;
  observer observer{};
  // Optional host hooks consulted while executing actions. Called on the
  // thread that runs the event tap (the one `start` was called from).
  control::delegate delegate{};
};

class runtime final {
 public:
  runtime() noexcept;
  runtime(runtime &&other) noexcept;
  auto operator=(runtime &&other) noexcept -> runtime &;

  runtime(const runtime &) = delete;
  auto operator=(const runtime &) -> runtime & = delete;

  ~runtime();

  [[nodiscard]] auto start(const options &options) -> std::expected<void, error>;
  void stop() noexcept;

  [[nodiscard]] auto reload_settings() -> std::expected<void, error>;
  [[nodiscard]] auto current_workspace_state() const -> std::expected<workspace_state, error>;

  // Suspends or resumes interception of global input (hotkeys and gestures)
  // without tearing down the runtime. Used so shortcut editors can capture a
  // key combination without the runtime acting on it. No-op when not running.
  void set_input_suspended(bool suspended) noexcept;

  [[nodiscard]] auto running() const noexcept -> bool;

 private:
  class impl;
  std::unique_ptr<impl> impl_{};
};

}  // namespace spacehound::daemon
