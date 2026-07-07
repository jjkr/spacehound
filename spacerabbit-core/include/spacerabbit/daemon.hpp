#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <spacerabbit/export.hpp>

namespace spacerabbit::daemon {

enum class exit_code {
  success = 0,
  invalid_usage = 1,
  settings_error = 2,
  permission_error = 3,
  runtime_error = 4,
};

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
  bool request_input_monitoring_if_needed = true;
  observer observer{};
};

class SPACERABBIT_EXPORT runtime final {
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

  [[nodiscard]] auto running() const noexcept -> bool;

 private:
  class impl;
  std::unique_ptr<impl> impl_{};
};

[[nodiscard]] SPACERABBIT_EXPORT auto run_foreground(const options &options) -> exit_code;

}  // namespace spacerabbit::daemon
