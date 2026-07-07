#include <spacerabbit/ns.hpp>

#import <AppKit/AppKit.h>

#include <utility>

namespace {

auto retain_object(id object) noexcept -> void * {
  if (object == nil) {
    return nullptr;
  }

  return reinterpret_cast<void *>([object retain]);
}

void release_object(void *object) noexcept {
  if (object == nullptr) {
    return;
  }

  [reinterpret_cast<id>(object) release];
}

template <typename T>
auto cast_object(void *object) noexcept -> T * {
  return reinterpret_cast<T *>(object);
}

template <typename T>
auto cast_object(const void *object) noexcept -> T * {
  return reinterpret_cast<T *>(const_cast<void *>(object));
}

}  // namespace

namespace spacerabbit::ns {

void running_application::reset() noexcept {
  release_object(object_);
  object_ = nullptr;
}

running_application::~running_application() {
  reset();
}

auto running_application::with_process_identifier(pid_t pid) noexcept
    -> running_application {
  return running_application{retain_object(
      [NSRunningApplication runningApplicationWithProcessIdentifier:pid])};
}

auto running_application::is_active() const noexcept -> bool {
  if (object_ == nullptr) {
    return false;
  }

  return [cast_object<NSRunningApplication>(object_) isActive];
}

auto running_application::activate(application_activation_options options) const noexcept
    -> bool {
  if (object_ == nullptr) {
    return false;
  }

  return [cast_object<NSRunningApplication>(object_)
      activateWithOptions:static_cast<NSApplicationActivationOptions>(options)];
}

void screen::reset() noexcept {
  release_object(object_);
  object_ = nullptr;
}

screen::~screen() {
  reset();
}

auto screen::main() noexcept -> screen {
  return screen{retain_object([NSScreen mainScreen])};
}

auto screen::backing_scale_factor() const noexcept -> double {
  if (object_ == nullptr) {
    return 0.0;
  }

  return static_cast<double>([cast_object<NSScreen>(object_) backingScaleFactor]);
}

void notification_center::reset() noexcept {
  release_object(object_);
  object_ = nullptr;
}

notification_center::~notification_center() {
  reset();
}

auto notification_center::add_observer(
    cf::string_view notification_name,
    notification_callback callback,
    void *context) const noexcept -> notification_observer {
  if (object_ == nullptr || !notification_name || callback == nullptr) {
    return {};
  }

  NSNotificationCenter *center = cast_object<NSNotificationCenter>(object_);
  NSString *name = cast_object<NSString>(notification_name.get());
  id token = [center addObserverForName:name
                                 object:nil
                                  queue:nil
                             usingBlock:^(__unused NSNotification *notification) {
                               callback(context);
                             }];
  if (token == nil) {
    return {};
  }

  return notification_observer{retain_object(center), retain_object(token)};
}

void notification_observer::reset() noexcept {
  if (center_ != nullptr && token_ != nullptr) {
    [cast_object<NSNotificationCenter>(center_)
        removeObserver:reinterpret_cast<id>(token_)];
  }

  release_object(token_);
  release_object(center_);
  token_ = nullptr;
  center_ = nullptr;
}

notification_observer::~notification_observer() {
  reset();
}

void workspace::reset() noexcept {
  release_object(object_);
  object_ = nullptr;
}

workspace::~workspace() {
  reset();
}

auto workspace::shared() noexcept -> workspace {
  return workspace{retain_object([NSWorkspace sharedWorkspace])};
}

auto workspace::notification_center() const noexcept -> class notification_center {
  if (object_ == nullptr) {
    return {};
  }

  return spacerabbit::ns::notification_center{
      retain_object([cast_object<NSWorkspace>(object_) notificationCenter])};
}

auto active_space_did_change_notification() noexcept -> cf::string_view {
  return cf::string_view{reinterpret_cast<CFStringRef>(
      NSWorkspaceActiveSpaceDidChangeNotification)};
}

}  // namespace spacerabbit::ns
