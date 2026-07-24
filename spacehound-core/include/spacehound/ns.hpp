#pragma once

#include <sys/types.h>

#include <cstdint>
#include <utility>

#include <spacehound/cf.hpp>

namespace spacehound::ns {

using application_activation_options = std::uint64_t;
using notification_callback = void (*)(void *);

inline constexpr auto activate_all_windows =
    application_activation_options{1} << 0;
inline constexpr auto activate_ignoring_other_apps =
    application_activation_options{1} << 1;

class notification_center;
class notification_observer;

/// A move-only owning wrapper around `NSRunningApplication *`.
class running_application final {
 public:
  /// Creates an empty running-application wrapper.
  running_application() noexcept = default;
  /// Creates an empty running-application wrapper from `nullptr`.
  running_application(std::nullptr_t) noexcept {}
  /// Transfers ownership from another wrapper.
  running_application(running_application &&other) noexcept
      : object_(std::exchange(other.object_, nullptr)) {}
  /// Replaces this wrapper with ownership from another wrapper.
  auto operator=(running_application &&other) noexcept -> running_application & {
    if (this != &other) {
      reset();
      object_ = std::exchange(other.object_, nullptr);
    }

    return *this;
  }

  running_application(const running_application &) = delete;
  auto operator=(const running_application &) -> running_application & = delete;

  ~running_application();

  /// Creates a running-application wrapper for a pid via `runningApplicationWithProcessIdentifier:`.
  [[nodiscard]] static auto with_process_identifier(pid_t pid) noexcept -> running_application;

  /// Returns whether the wrapper holds an application object.
  [[nodiscard]] explicit operator bool() const noexcept {
    return object_ != nullptr;
  }

  /// Returns whether the application is currently active.
  [[nodiscard]] auto is_active() const noexcept -> bool;

  /// Activates the application with the requested options.
  [[nodiscard]] auto activate(application_activation_options options) const noexcept -> bool;

 private:
  explicit running_application(void *object) noexcept : object_(object) {}

  void reset() noexcept;

  void *object_ = nullptr;
};

/// A move-only owning wrapper around `NSScreen *`.
class screen final {
 public:
  screen() noexcept = default;
  screen(std::nullptr_t) noexcept {}
  screen(screen &&other) noexcept : object_(std::exchange(other.object_, nullptr)) {}
  auto operator=(screen &&other) noexcept -> screen & {
    if (this != &other) {
      reset();
      object_ = std::exchange(other.object_, nullptr);
    }

    return *this;
  }

  screen(const screen &) = delete;
  auto operator=(const screen &) -> screen & = delete;

  ~screen();

  /// Returns the main screen via `NSScreen.mainScreen`.
  [[nodiscard]] static auto main() noexcept -> screen;

  /// Returns whether the wrapper holds a screen.
  [[nodiscard]] explicit operator bool() const noexcept {
    return object_ != nullptr;
  }

  /// Returns the screen backing scale factor.
  [[nodiscard]] auto backing_scale_factor() const noexcept -> double;

 private:
  explicit screen(void *object) noexcept : object_(object) {}

  void reset() noexcept;

  void *object_ = nullptr;
};

/// A move-only owning wrapper around `NSNotificationCenter *`.
class notification_center final {
 public:
  notification_center() noexcept = default;
  notification_center(std::nullptr_t) noexcept {}
  notification_center(notification_center &&other) noexcept
      : object_(std::exchange(other.object_, nullptr)) {}
  auto operator=(notification_center &&other) noexcept -> notification_center & {
    if (this != &other) {
      reset();
      object_ = std::exchange(other.object_, nullptr);
    }

    return *this;
  }

  notification_center(const notification_center &) = delete;
  auto operator=(const notification_center &) -> notification_center & = delete;

  ~notification_center();

  /// Returns whether the wrapper holds a notification center.
  [[nodiscard]] explicit operator bool() const noexcept {
    return object_ != nullptr;
  }

  /// Adds an observer token for the named notification.
  [[nodiscard]] auto add_observer(
      cf::string_view notification_name,
      notification_callback callback,
      void *context) const noexcept -> notification_observer;

 private:
  explicit notification_center(void *object) noexcept : object_(object) {}

  friend class workspace;
  friend class notification_observer;

  void reset() noexcept;

  void *object_ = nullptr;
};

/// A move-only owning wrapper around the observer token returned by `addObserverForName:...`.
class notification_observer final {
 public:
  notification_observer() noexcept = default;
  notification_observer(std::nullptr_t) noexcept {}
  notification_observer(notification_observer &&other) noexcept
      : center_(std::exchange(other.center_, nullptr)),
        token_(std::exchange(other.token_, nullptr)) {}
  auto operator=(notification_observer &&other) noexcept -> notification_observer & {
    if (this != &other) {
      reset();
      center_ = std::exchange(other.center_, nullptr);
      token_ = std::exchange(other.token_, nullptr);
    }

    return *this;
  }

  notification_observer(const notification_observer &) = delete;
  auto operator=(const notification_observer &) -> notification_observer & = delete;

  ~notification_observer();

  /// Returns whether the wrapper holds an observer token.
  [[nodiscard]] explicit operator bool() const noexcept {
    return token_ != nullptr;
  }

 private:
  notification_observer(void *center, void *token) noexcept
      : center_(center), token_(token) {}

  friend class notification_center;

  void reset() noexcept;

  void *center_ = nullptr;
  void *token_ = nullptr;
};

/// A move-only owning wrapper around `NSWorkspace *`.
class workspace final {
 public:
  workspace() noexcept = default;
  workspace(std::nullptr_t) noexcept {}
  workspace(workspace &&other) noexcept : object_(std::exchange(other.object_, nullptr)) {}
  auto operator=(workspace &&other) noexcept -> workspace & {
    if (this != &other) {
      reset();
      object_ = std::exchange(other.object_, nullptr);
    }

    return *this;
  }

  workspace(const workspace &) = delete;
  auto operator=(const workspace &) -> workspace & = delete;

  ~workspace();

  /// Returns the shared workspace via `NSWorkspace.sharedWorkspace`.
  [[nodiscard]] static auto shared() noexcept -> workspace;

  /// Returns whether the wrapper holds a workspace object.
  [[nodiscard]] explicit operator bool() const noexcept {
    return object_ != nullptr;
  }

  /// Returns the workspace notification center.
  [[nodiscard]] auto notification_center() const noexcept -> class notification_center;

 private:
  explicit workspace(void *object) noexcept : object_(object) {}

  void reset() noexcept;

  void *object_ = nullptr;
};

/// Returns `NSWorkspaceActiveSpaceDidChangeNotification` as a Core Foundation string view.
[[nodiscard]] auto active_space_did_change_notification() noexcept -> cf::string_view;

}  // namespace spacehound::ns
