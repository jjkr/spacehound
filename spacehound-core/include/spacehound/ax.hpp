#pragma once

#include <ApplicationServices/ApplicationServices.h>
#include <sys/types.h>

#include <utility>

#include <spacehound/cf.hpp>

extern "C" {
// Private AX helper used by SpaceHound to map a window element back to its Window Server id.
extern AXError _AXUIElementGetWindow(AXUIElementRef element, CGWindowID *window);
}

namespace spacehound::ax {

class value;
class value_view;
class ui_element;
class ui_element_view;
class observer;
class observer_view;

inline const auto trusted_check_option_prompt =
    cf::string_view{kAXTrustedCheckOptionPrompt};
inline const auto windows_attribute = cf::string_view{kAXWindowsAttribute};
inline const auto focused_window_attribute =
    cf::string_view{kAXFocusedWindowAttribute};
inline const auto main_attribute = cf::string_view{kAXMainAttribute};
inline const auto title_attribute = cf::string_view{kAXTitleAttribute};
inline const auto position_attribute = cf::string_view{kAXPositionAttribute};
inline const auto size_attribute = cf::string_view{kAXSizeAttribute};
inline const auto frame_attribute = cf::string_view{CFSTR("AXFrame")};
inline const auto children_attribute = cf::string_view{kAXChildrenAttribute};
inline const auto selected_children_attribute =
    cf::string_view{kAXSelectedChildrenAttribute};
inline const auto identifier_attribute = cf::string_view{kAXIdentifierAttribute};
inline const auto role_attribute = cf::string_view{kAXRoleAttribute};
inline const auto menu_bar_role = cf::string_view{kAXMenuBarRole};
inline const auto raise_action = cf::string_view{kAXRaiseAction};

/// Returns whether the current process is trusted for Accessibility access.
[[nodiscard]] inline auto is_process_trusted() noexcept -> bool {
  return AXIsProcessTrusted() != 0;
}

/// Returns whether the current process is trusted, using the supplied options dictionary.
[[nodiscard]] inline auto is_process_trusted_with_options(
    cf::dictionary_view options) noexcept -> bool {
  return AXIsProcessTrustedWithOptions(options.get()) != 0;
}

/// A lightweight non-owning wrapper around `AXValueRef`.
class value_view final {
 public:
  /// Creates an empty non-owning AX value view.
  constexpr value_view() noexcept = default;
  /// Creates an empty non-owning AX value view from `nullptr`.
  constexpr value_view(std::nullptr_t) noexcept {}
  /// Wraps a raw `AXValueRef` without retaining it.
  explicit constexpr value_view(AXValueRef ref) noexcept : ref_(cf::view{ref}) {}
  /// Wraps an existing AX value view reference.
  explicit constexpr value_view(cf::view<AXValueRef> ref) noexcept : ref_(ref) {}

  /// Returns the wrapped raw AX value reference.
  [[nodiscard]] constexpr auto get() const noexcept -> AXValueRef {
    return ref_.get();
  }

  /// Returns whether the view references an AX value.
  [[nodiscard]] explicit constexpr operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Returns the underlying AX value type via `AXValueGetType`.
  [[nodiscard]] auto type() const noexcept -> AXValueType {
    if (!ref_) {
      return static_cast<AXValueType>(kAXValueIllegalType);
    }

    return AXValueGetType(get());
  }

  /// Extracts a point via `AXValueGetValue`.
  [[nodiscard]] auto get_value(CGPoint &value) const noexcept -> bool {
    return AXValueGetValue(
               get(), static_cast<AXValueType>(kAXValueCGPointType), &value) != 0;
  }

  /// Extracts a size via `AXValueGetValue`.
  [[nodiscard]] auto get_value(CGSize &value) const noexcept -> bool {
    return AXValueGetValue(
               get(), static_cast<AXValueType>(kAXValueCGSizeType), &value) != 0;
  }

  /// Extracts a rectangle via `AXValueGetValue`.
  [[nodiscard]] auto get_value(CGRect &value) const noexcept -> bool {
    return AXValueGetValue(
               get(), static_cast<AXValueType>(kAXValueCGRectType), &value) != 0;
  }

 private:
  cf::view<AXValueRef> ref_{};
};

/// A move-only owning wrapper around `AXValueRef`.
class value final {
 public:
  /// Creates an empty owning AX value.
  value() noexcept = default;
  /// Creates an empty owning AX value from `nullptr`.
  value(std::nullptr_t) noexcept {}
  /// Transfers ownership from another AX value wrapper.
  value(value &&) noexcept = default;
  /// Replaces this wrapper with ownership from another AX value wrapper.
  auto operator=(value &&) noexcept -> value & = default;

  /// AX value wrappers are intentionally move-only.
  value(const value &) = delete;
  /// AX value wrappers are intentionally move-only.
  auto operator=(const value &) -> value & = delete;

  /// Adopts an already-retained `AXValueRef`.
  [[nodiscard]] static auto adopt(AXValueRef ref) noexcept -> value {
    return value{cf::adopt(ref)};
  }

  /// Retains an AX value view reference with `CFRetain`.
  [[nodiscard]] static auto retain(value_view ref) noexcept -> value {
    return value{cf::retain(ref.get())};
  }

  /// Creates an AX point value with `AXValueCreate`.
  [[nodiscard]] static auto create(CGPoint point) noexcept -> value {
    return adopt(AXValueCreate(
        static_cast<AXValueType>(kAXValueCGPointType), &point));
  }

  /// Creates an AX size value with `AXValueCreate`.
  [[nodiscard]] static auto create(CGSize size) noexcept -> value {
    return adopt(AXValueCreate(
        static_cast<AXValueType>(kAXValueCGSizeType), &size));
  }

  /// Creates an AX rectangle value with `AXValueCreate`.
  [[nodiscard]] static auto create(CGRect rect) noexcept -> value {
    return adopt(AXValueCreate(
        static_cast<AXValueType>(kAXValueCGRectType), &rect));
  }

  /// Returns the wrapped raw AX value reference.
  [[nodiscard]] auto get() const noexcept -> AXValueRef {
    return ref_.get();
  }

  /// Returns whether the wrapper owns an AX value.
  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Returns a non-owning view of the wrapped AX value.
  [[nodiscard]] auto view() const noexcept -> value_view {
    return value_view{get()};
  }

  /// Returns the underlying AX value type via `AXValueGetType`.
  [[nodiscard]] auto type() const noexcept -> AXValueType {
    return view().type();
  }

  /// Extracts a point via `AXValueGetValue`.
  [[nodiscard]] auto get_value(CGPoint &point) const noexcept -> bool {
    return view().get_value(point);
  }

  /// Extracts a size via `AXValueGetValue`.
  [[nodiscard]] auto get_value(CGSize &size) const noexcept -> bool {
    return view().get_value(size);
  }

  /// Extracts a rectangle via `AXValueGetValue`.
  [[nodiscard]] auto get_value(CGRect &rect) const noexcept -> bool {
    return view().get_value(rect);
  }

 private:
  explicit value(cf::retained<AXValueRef> ref) noexcept : ref_(std::move(ref)) {}

  cf::retained<AXValueRef> ref_{};
};

/// A lightweight non-owning wrapper around `AXUIElementRef`.
class ui_element_view final {
 public:
  /// Creates an empty non-owning AX UI element view.
  constexpr ui_element_view() noexcept = default;
  /// Creates an empty non-owning AX UI element view from `nullptr`.
  constexpr ui_element_view(std::nullptr_t) noexcept {}
  /// Wraps a raw `AXUIElementRef` without retaining it.
  explicit constexpr ui_element_view(AXUIElementRef ref) noexcept : ref_(cf::view{ref}) {}
  /// Wraps an existing AX UI element view reference.
  explicit constexpr ui_element_view(cf::view<AXUIElementRef> ref) noexcept : ref_(ref) {}

  /// Returns the wrapped raw AX UI element reference.
  [[nodiscard]] constexpr auto get() const noexcept -> AXUIElementRef {
    return ref_.get();
  }

  /// Returns whether the view references an AX UI element.
  [[nodiscard]] explicit constexpr operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Copies an AX attribute value via `AXUIElementCopyAttributeValue`.
  auto copy_attribute_value(cf::string_view attribute, cf::type &value) const noexcept
      -> AXError {
    CFTypeRef copied_value = nullptr;
    const AXError error =
        AXUIElementCopyAttributeValue(get(), attribute.get(), &copied_value);
    if (error != kAXErrorSuccess) {
      value = {};
      return error;
    }

    value = cf::type::adopt(copied_value);
    return error;
  }

  /// Sets an AX attribute value via `AXUIElementSetAttributeValue`.
  auto set_attribute_value(cf::string_view attribute, cf::type_view value) const noexcept
      -> AXError {
    return AXUIElementSetAttributeValue(get(), attribute.get(), value.get());
  }

  /// Performs an AX action via `AXUIElementPerformAction`.
  auto perform_action(cf::string_view action) const noexcept -> AXError {
    return AXUIElementPerformAction(get(), action.get());
  }

  /// Resolves the Window Server id for the element via `_AXUIElementGetWindow`.
  auto get_window(CGWindowID &window) const noexcept -> AXError {
    return _AXUIElementGetWindow(get(), &window);
  }

  /// Returns the owning process id via `AXUIElementGetPid`.
  auto get_pid(pid_t &pid) const noexcept -> AXError {
    return AXUIElementGetPid(get(), &pid);
  }

  /// Copies the element under a screen point via `AXUIElementCopyElementAtPosition`.
  /// Meaningful on the system-wide element; coordinates use the top-left origin.
  auto copy_element_at_position(CGPoint point, AXUIElementRef &out_element) const noexcept
      -> AXError {
    out_element = nullptr;
    return AXUIElementCopyElementAtPosition(
        get(), static_cast<float>(point.x), static_cast<float>(point.y), &out_element);
  }

 private:
  cf::view<AXUIElementRef> ref_{};
};

/// A move-only owning wrapper around `AXUIElementRef`.
class ui_element final {
 public:
  /// Creates an empty owning AX UI element.
  ui_element() noexcept = default;
  /// Creates an empty owning AX UI element from `nullptr`.
  ui_element(std::nullptr_t) noexcept {}
  /// Transfers ownership from another AX UI element wrapper.
  ui_element(ui_element &&) noexcept = default;
  /// Replaces this wrapper with ownership from another AX UI element wrapper.
  auto operator=(ui_element &&) noexcept -> ui_element & = default;

  /// AX UI element wrappers are intentionally move-only.
  ui_element(const ui_element &) = delete;
  /// AX UI element wrappers are intentionally move-only.
  auto operator=(const ui_element &) -> ui_element & = delete;

  /// Adopts an already-retained `AXUIElementRef`.
  [[nodiscard]] static auto adopt(AXUIElementRef ref) noexcept -> ui_element {
    return ui_element{cf::adopt(ref)};
  }

  /// Retains an AX UI element view reference with `CFRetain`.
  [[nodiscard]] static auto retain(ui_element_view ref) noexcept -> ui_element {
    return ui_element{cf::retain(ref.get())};
  }

  /// Creates an AX application element with `AXUIElementCreateApplication`.
  [[nodiscard]] static auto create_application(pid_t pid) noexcept -> ui_element {
    return adopt(AXUIElementCreateApplication(pid));
  }

  /// Creates the system-wide AX element with `AXUIElementCreateSystemWide`.
  [[nodiscard]] static auto create_system_wide() noexcept -> ui_element {
    return adopt(AXUIElementCreateSystemWide());
  }

  /// Returns the wrapped raw AX UI element reference.
  [[nodiscard]] auto get() const noexcept -> AXUIElementRef {
    return ref_.get();
  }

  /// Returns whether the wrapper owns an AX UI element.
  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Returns a non-owning view of the wrapped AX UI element.
  [[nodiscard]] auto view() const noexcept -> ui_element_view {
    return ui_element_view{get()};
  }

  /// Copies an AX attribute value via `AXUIElementCopyAttributeValue`.
  auto copy_attribute_value(cf::string_view attribute, cf::type &value) const noexcept
      -> AXError {
    return view().copy_attribute_value(attribute, value);
  }

  /// Sets an AX attribute value via `AXUIElementSetAttributeValue`.
  auto set_attribute_value(cf::string_view attribute, cf::type_view value) const noexcept
      -> AXError {
    return view().set_attribute_value(attribute, value);
  }

  /// Performs an AX action via `AXUIElementPerformAction`.
  auto perform_action(cf::string_view action) const noexcept -> AXError {
    return view().perform_action(action);
  }

  /// Resolves the Window Server id for the element via `_AXUIElementGetWindow`.
  auto get_window(CGWindowID &window) const noexcept -> AXError {
    return view().get_window(window);
  }

 private:
  explicit ui_element(cf::retained<AXUIElementRef> ref) noexcept
      : ref_(std::move(ref)) {}

  cf::retained<AXUIElementRef> ref_{};
};

/// A lightweight non-owning wrapper around `AXObserverRef`.
class observer_view final {
 public:
  /// Creates an empty non-owning AX observer view.
  constexpr observer_view() noexcept = default;
  /// Creates an empty non-owning AX observer view from `nullptr`.
  constexpr observer_view(std::nullptr_t) noexcept {}
  /// Wraps a raw `AXObserverRef` without retaining it.
  explicit constexpr observer_view(AXObserverRef ref) noexcept : ref_(cf::view{ref}) {}
  /// Wraps an existing AX observer view reference.
  explicit constexpr observer_view(cf::view<AXObserverRef> ref) noexcept : ref_(ref) {}

  /// Returns the wrapped raw AX observer reference.
  [[nodiscard]] constexpr auto get() const noexcept -> AXObserverRef {
    return ref_.get();
  }

  /// Returns whether the view references an AX observer.
  [[nodiscard]] explicit constexpr operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Adds an AX notification via `AXObserverAddNotification`.
  auto add_notification(
      ui_element_view element,
      cf::string_view notification,
      void *refcon) const noexcept -> AXError {
    return AXObserverAddNotification(get(), element.get(), notification.get(), refcon);
  }

  /// Returns the observer run-loop source via `AXObserverGetRunLoopSource`.
  [[nodiscard]] auto run_loop_source() const noexcept -> cf::view<CFRunLoopSourceRef> {
    return cf::view{AXObserverGetRunLoopSource(get())};
  }

 private:
  cf::view<AXObserverRef> ref_{};
};

/// A move-only owning wrapper around `AXObserverRef`.
class observer final {
 public:
  /// Creates an empty owning AX observer.
  observer() noexcept = default;
  /// Creates an empty owning AX observer from `nullptr`.
  observer(std::nullptr_t) noexcept {}
  /// Transfers ownership from another AX observer wrapper.
  observer(observer &&) noexcept = default;
  /// Replaces this wrapper with ownership from another AX observer wrapper.
  auto operator=(observer &&) noexcept -> observer & = default;

  /// AX observer wrappers are intentionally move-only.
  observer(const observer &) = delete;
  /// AX observer wrappers are intentionally move-only.
  auto operator=(const observer &) -> observer & = delete;

  /// Adopts an already-retained `AXObserverRef`.
  [[nodiscard]] static auto adopt(AXObserverRef ref) noexcept -> observer {
    return observer{cf::adopt(ref)};
  }

  /// Retains an AX observer view reference with `CFRetain`.
  [[nodiscard]] static auto retain(observer_view ref) noexcept -> observer {
    return observer{cf::retain(ref.get())};
  }

  /// Creates an AX observer with `AXObserverCreate`.
  [[nodiscard]] static auto create(
      pid_t pid,
      AXObserverCallback callback,
      AXError *error = nullptr) noexcept -> observer {
    AXObserverRef ref = nullptr;
    const AXError create_error = AXObserverCreate(pid, callback, &ref);
    if (error != nullptr) {
      *error = create_error;
    }

    if (create_error != kAXErrorSuccess) {
      return {};
    }

    return adopt(ref);
  }

  /// Returns the wrapped raw AX observer reference.
  [[nodiscard]] auto get() const noexcept -> AXObserverRef {
    return ref_.get();
  }

  /// Returns whether the wrapper owns an AX observer.
  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Returns a non-owning view of the wrapped AX observer.
  [[nodiscard]] auto view() const noexcept -> observer_view {
    return observer_view{get()};
  }

  /// Adds an AX notification via `AXObserverAddNotification`.
  auto add_notification(
      ui_element_view element,
      cf::string_view notification,
      void *refcon) const noexcept -> AXError {
    return view().add_notification(element, notification, refcon);
  }

  /// Returns the observer run-loop source via `AXObserverGetRunLoopSource`.
  [[nodiscard]] auto run_loop_source() const noexcept -> cf::view<CFRunLoopSourceRef> {
    return view().run_loop_source();
  }

 private:
  explicit observer(cf::retained<AXObserverRef> ref) noexcept : ref_(std::move(ref)) {}

  cf::retained<AXObserverRef> ref_{};
};

}  // namespace spacehound::ax
