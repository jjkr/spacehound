// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <ApplicationServices/ApplicationServices.h>
#include <sys/types.h>

#include <cstdint>
#include <utility>
#include <vector>

#include <spacehound/cf.hpp>

namespace spacehound::cg {

class event;
class event_view;
class event_source;
class event_source_view;
class event_tap;
class event_tap_view;

inline const auto window_number_key = cf::string_view{kCGWindowNumber};
inline const auto window_owner_pid_key = cf::string_view{kCGWindowOwnerPID};
inline const auto window_layer_key = cf::string_view{kCGWindowLayer};
inline const auto window_name_key = cf::string_view{kCGWindowName};
inline const auto window_owner_name_key = cf::string_view{kCGWindowOwnerName};
inline const auto window_bounds_key = cf::string_view{kCGWindowBounds};
inline const auto window_alpha_key = cf::string_view{kCGWindowAlpha};
inline const auto window_is_onscreen_key = cf::string_view{kCGWindowIsOnscreen};

inline constexpr auto gesture_event_type = static_cast<CGEventType>(30);
inline constexpr auto event_type_field = static_cast<CGEventField>(0x37);
inline constexpr auto gesture_subtype_field = static_cast<CGEventField>(0x6e);
inline constexpr auto gesture_type_field = static_cast<CGEventField>(0x7b);
inline constexpr auto gesture_delta_field = static_cast<CGEventField>(0x7c);
inline constexpr auto gesture_momentum_x_field = static_cast<CGEventField>(0x81);
inline constexpr auto gesture_momentum_y_field = static_cast<CGEventField>(0x82);
inline constexpr auto gesture_phase_field = static_cast<CGEventField>(0x84);
inline constexpr auto gesture_phase_mirror_field = static_cast<CGEventField>(0x86);
inline constexpr auto gesture_finger_count_field = static_cast<CGEventField>(0x8a);
inline constexpr auto gesture_type_mirror_field = static_cast<CGEventField>(0xa5);
inline constexpr auto scroll_momentum_phase_field = static_cast<CGEventField>(123);

/// A lightweight non-owning wrapper around `CGEventSourceRef` and related Quartz event-source APIs.
class event_source_view final {
 public:
  /// Creates an empty non-owning event-source view.
  constexpr event_source_view() noexcept = default;
  /// Creates an empty non-owning event-source view from `nullptr`.
  constexpr event_source_view(std::nullptr_t) noexcept {}
  /// Wraps a raw `CGEventSourceRef` without retaining it.
  explicit constexpr event_source_view(CGEventSourceRef ref) noexcept : ref_(cf::view{ref}) {}
  /// Wraps an existing event-source view reference.
  explicit constexpr event_source_view(cf::view<CGEventSourceRef> ref) noexcept : ref_(ref) {}

  /// Returns the wrapped raw event-source reference.
  [[nodiscard]] constexpr auto get() const noexcept -> CGEventSourceRef {
    return ref_.get();
  }

  /// Returns whether the view references an event source.
  [[nodiscard]] explicit constexpr operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Returns the keyboard type via `CGEventSourceGetKeyboardType`.
  [[nodiscard]] auto keyboard_type() const noexcept -> CGEventSourceKeyboardType {
    return CGEventSourceGetKeyboardType(get());
  }

  /// Updates the keyboard type via `CGEventSourceSetKeyboardType`.
  void set_keyboard_type(CGEventSourceKeyboardType keyboard_type) const noexcept {
    CGEventSourceSetKeyboardType(get(), keyboard_type);
  }

  /// Returns the scrolling resolution via `CGEventSourceGetPixelsPerLine`.
  [[nodiscard]] auto pixels_per_line() const noexcept -> double {
    return CGEventSourceGetPixelsPerLine(get());
  }

  /// Updates the scrolling resolution via `CGEventSourceSetPixelsPerLine`.
  void set_pixels_per_line(double pixels_per_line) const noexcept {
    CGEventSourceSetPixelsPerLine(get(), pixels_per_line);
  }

  /// Returns the source state via `CGEventSourceGetSourceStateID`.
  [[nodiscard]] auto source_state_id() const noexcept -> CGEventSourceStateID {
    return CGEventSourceGetSourceStateID(get());
  }

  /// Returns the user payload via `CGEventSourceGetUserData`.
  [[nodiscard]] auto user_data() const noexcept -> std::int64_t {
    return CGEventSourceGetUserData(get());
  }

  /// Attaches a user payload via `CGEventSourceSetUserData`.
  void set_user_data(std::int64_t user_data) const noexcept {
    CGEventSourceSetUserData(get(), user_data);
  }

 private:
  cf::view<CGEventSourceRef> ref_{};
};

/// A lightweight non-owning wrapper around `CGEventRef` and related Quartz event APIs.
class event_view final {
 public:
  /// Creates an empty non-owning event view.
  constexpr event_view() noexcept = default;
  /// Creates an empty non-owning event view from `nullptr`.
  constexpr event_view(std::nullptr_t) noexcept {}
  /// Wraps a raw `CGEventRef` without retaining it.
  explicit constexpr event_view(CGEventRef ref) noexcept : ref_(cf::view{ref}) {}
  /// Wraps an existing event view reference.
  explicit constexpr event_view(cf::view<CGEventRef> ref) noexcept : ref_(ref) {}

  /// Returns the wrapped raw event reference.
  [[nodiscard]] constexpr auto get() const noexcept -> CGEventRef {
    return ref_.get();
  }

  /// Returns whether the view references an event.
  [[nodiscard]] explicit constexpr operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Returns the event type via `CGEventGetType`.
  [[nodiscard]] auto type() const noexcept -> CGEventType {
    return CGEventGetType(get());
  }

  /// Updates the event type via `CGEventSetType`.
  void set_type(CGEventType type) const noexcept {
    CGEventSetType(get(), type);
  }

  /// Returns the event timestamp via `CGEventGetTimestamp`.
  [[nodiscard]] auto timestamp() const noexcept -> CGEventTimestamp {
    return CGEventGetTimestamp(get());
  }

  /// Updates the event timestamp via `CGEventSetTimestamp`.
  void set_timestamp(CGEventTimestamp timestamp) const noexcept {
    CGEventSetTimestamp(get(), timestamp);
  }

  /// Returns the event location via `CGEventGetLocation`.
  [[nodiscard]] auto location() const noexcept -> CGPoint {
    return CGEventGetLocation(get());
  }

  /// Updates the event location via `CGEventSetLocation`.
  void set_location(CGPoint location) const noexcept {
    CGEventSetLocation(get(), location);
  }

  /// Returns the modifier flags via `CGEventGetFlags`.
  [[nodiscard]] auto flags() const noexcept -> CGEventFlags {
    return CGEventGetFlags(get());
  }

  /// Updates the modifier flags via `CGEventSetFlags`.
  void set_flags(CGEventFlags flags) const noexcept {
    CGEventSetFlags(get(), flags);
  }

  /// Returns an integer event field via `CGEventGetIntegerValueField`.
  [[nodiscard]] auto integer_field(CGEventField field) const noexcept -> std::int64_t {
    return CGEventGetIntegerValueField(get(), field);
  }

  /// Updates an integer event field via `CGEventSetIntegerValueField`.
  void set_integer_field(CGEventField field, std::int64_t value) const noexcept {
    CGEventSetIntegerValueField(get(), field, value);
  }

  /// Returns a floating-point event field via `CGEventGetDoubleValueField`.
  [[nodiscard]] auto double_field(CGEventField field) const noexcept -> double {
    return CGEventGetDoubleValueField(get(), field);
  }

  /// Updates a floating-point event field via `CGEventSetDoubleValueField`.
  void set_double_field(CGEventField field, double value) const noexcept {
    CGEventSetDoubleValueField(get(), field, value);
  }

  /// Serializes the event with `CGEventCreateData`.
  [[nodiscard]] auto create_data() const noexcept -> cf::retained<CFDataRef> {
    return cf::adopt(CGEventCreateData(kCFAllocatorDefault, get()));
  }

  /// Posts the event with `CGEventPost`.
  void post(CGEventTapLocation tap_location) const noexcept {
    CGEventPost(tap_location, get());
  }

  /// Posts the event to a process with `CGEventPostToPid`.
  void post_to_pid(pid_t pid) const noexcept {
    CGEventPostToPid(pid, get());
  }

 private:
  cf::view<CGEventRef> ref_{};
};

/// A lightweight non-owning wrapper around `CFMachPortRef` for Quartz event taps.
class event_tap_view final {
 public:
  /// Creates an empty non-owning event-tap view.
  constexpr event_tap_view() noexcept = default;
  /// Creates an empty non-owning event-tap view from `nullptr`.
  constexpr event_tap_view(std::nullptr_t) noexcept {}
  /// Wraps a raw `CFMachPortRef` without retaining it.
  explicit constexpr event_tap_view(CFMachPortRef ref) noexcept : ref_(cf::view{ref}) {}
  /// Wraps an existing event-tap view reference.
  explicit constexpr event_tap_view(cf::view<CFMachPortRef> ref) noexcept : ref_(ref) {}

  /// Returns the wrapped raw event-tap reference.
  [[nodiscard]] constexpr auto get() const noexcept -> CFMachPortRef {
    return ref_.get();
  }

  /// Returns whether the view references an event tap.
  [[nodiscard]] explicit constexpr operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Enables or disables the event tap via `CGEventTapEnable`.
  void enable(bool enabled) const noexcept {
    if (!ref_) {
      return;
    }

    CGEventTapEnable(get(), enabled);
  }

  /// Creates a run-loop source for the event tap via `CFMachPortCreateRunLoopSource`.
  [[nodiscard]] auto create_run_loop_source(CFIndex order = 0) const noexcept
      -> cf::retained<CFRunLoopSourceRef> {
    if (!ref_) {
      return {};
    }

    return cf::adopt(CFMachPortCreateRunLoopSource(
        kCFAllocatorDefault, get(), order));
  }

 private:
  cf::view<CFMachPortRef> ref_{};
};

/// A move-only owning wrapper around `CGEventSourceRef`.
class event_source final {
 public:
  /// Creates an empty owning event source.
  event_source() noexcept = default;
  /// Creates an empty owning event source from `nullptr`.
  event_source(std::nullptr_t) noexcept {}
  /// Transfers ownership from another event source wrapper.
  event_source(event_source &&) noexcept = default;
  /// Replaces this wrapper with ownership from another event source wrapper.
  auto operator=(event_source &&) noexcept -> event_source & = default;

  /// Event sources are intentionally move-only.
  event_source(const event_source &) = delete;
  /// Event sources are intentionally move-only.
  auto operator=(const event_source &) -> event_source & = delete;

  /// Adopts an already-retained `CGEventSourceRef` returned under Quartz create rules.
  [[nodiscard]] static auto adopt(CGEventSourceRef ref) noexcept -> event_source {
    return event_source{cf::adopt(ref)};
  }

  /// Retains an event-source view reference with `CFRetain`.
  [[nodiscard]] static auto retain(event_source_view view) noexcept -> event_source {
    return event_source{cf::retain(view.get())};
  }

  /// Creates a new Quartz event source with `CGEventSourceCreate`.
  [[nodiscard]] static auto create(CGEventSourceStateID state_id) noexcept -> event_source {
    return adopt(CGEventSourceCreate(state_id));
  }

  /// Returns the wrapped raw event-source reference.
  [[nodiscard]] auto get() const noexcept -> CGEventSourceRef {
    return ref_.get();
  }

  /// Returns whether the wrapper owns an event source.
  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Returns a non-owning view of the wrapped event source.
  [[nodiscard]] auto view() const noexcept -> event_source_view {
    return event_source_view{get()};
  }

  /// Returns the keyboard type associated with the source.
  [[nodiscard]] auto keyboard_type() const noexcept -> CGEventSourceKeyboardType {
    return view().keyboard_type();
  }

  /// Updates the keyboard type associated with the source.
  void set_keyboard_type(CGEventSourceKeyboardType keyboard_type) const noexcept {
    view().set_keyboard_type(keyboard_type);
  }

  /// Returns the scrolling resolution used by the source.
  [[nodiscard]] auto pixels_per_line() const noexcept -> double {
    return view().pixels_per_line();
  }

  /// Updates the scrolling resolution used by the source.
  void set_pixels_per_line(double pixels_per_line) const noexcept {
    view().set_pixels_per_line(pixels_per_line);
  }

  /// Returns the system state bucket the source was created from.
  [[nodiscard]] auto source_state_id() const noexcept -> CGEventSourceStateID {
    return view().source_state_id();
  }

  /// Returns the user-defined payload attached to the source.
  [[nodiscard]] auto user_data() const noexcept -> std::int64_t {
    return view().user_data();
  }

  /// Attaches a user-defined payload to the source.
  void set_user_data(std::int64_t user_data) const noexcept {
    view().set_user_data(user_data);
  }

 private:
  explicit event_source(cf::retained<CGEventSourceRef> ref) noexcept : ref_(std::move(ref)) {}

  cf::retained<CGEventSourceRef> ref_{};
};

/// A move-only owning wrapper around `CFMachPortRef` for Quartz event taps.
class event_tap final {
 public:
  /// Creates an empty owning event tap.
  event_tap() noexcept = default;
  /// Creates an empty owning event tap from `nullptr`.
  event_tap(std::nullptr_t) noexcept {}
  /// Transfers ownership from another event-tap wrapper.
  event_tap(event_tap &&) noexcept = default;
  /// Replaces this wrapper with ownership from another event-tap wrapper.
  auto operator=(event_tap &&) noexcept -> event_tap & = default;

  /// Event taps are intentionally move-only.
  event_tap(const event_tap &) = delete;
  /// Event taps are intentionally move-only.
  auto operator=(const event_tap &) -> event_tap & = delete;

  /// Adopts an already-retained `CFMachPortRef`.
  [[nodiscard]] static auto adopt(CFMachPortRef ref) noexcept -> event_tap {
    return event_tap{cf::adopt(ref)};
  }

  /// Retains an event-tap view reference with `CFRetain`.
  [[nodiscard]] static auto retain(event_tap_view view) noexcept -> event_tap {
    return event_tap{cf::retain(view.get())};
  }

  /// Creates a Quartz event tap with `CGEventTapCreate`.
  [[nodiscard]] static auto create(
      CGEventTapLocation tap,
      CGEventTapPlacement place,
      CGEventTapOptions options,
      CGEventMask events_of_interest,
      CGEventTapCallBack callback,
      void *user_info) noexcept -> event_tap {
    return adopt(CGEventTapCreate(
        tap, place, options, events_of_interest, callback, user_info));
  }

  /// Returns the wrapped raw event-tap reference.
  [[nodiscard]] auto get() const noexcept -> CFMachPortRef {
    return ref_.get();
  }

  /// Returns whether the wrapper owns an event tap.
  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Returns a non-owning view of the wrapped event tap.
  [[nodiscard]] auto view() const noexcept -> event_tap_view {
    return event_tap_view{get()};
  }

  /// Enables or disables the event tap via `CGEventTapEnable`.
  void enable(bool enabled) const noexcept {
    view().enable(enabled);
  }

  /// Creates a run-loop source for the event tap via `CFMachPortCreateRunLoopSource`.
  [[nodiscard]] auto create_run_loop_source(CFIndex order = 0) const noexcept
      -> cf::retained<CFRunLoopSourceRef> {
    return view().create_run_loop_source(order);
  }

 private:
  explicit event_tap(cf::retained<CFMachPortRef> ref) noexcept : ref_(std::move(ref)) {}

  cf::retained<CFMachPortRef> ref_{};
};

/// A move-only owning wrapper around `CGEventRef`.
class event final {
 public:
  /// Creates an empty owning event.
  event() noexcept = default;
  /// Creates an empty owning event from `nullptr`.
  event(std::nullptr_t) noexcept {}
  /// Transfers ownership from another event wrapper.
  event(event &&) noexcept = default;
  /// Replaces this wrapper with ownership from another event wrapper.
  auto operator=(event &&) noexcept -> event & = default;

  /// Events are intentionally move-only.
  event(const event &) = delete;
  /// Events are intentionally move-only.
  auto operator=(const event &) -> event & = delete;

  /// Adopts an already-retained `CGEventRef` returned under Quartz create rules.
  [[nodiscard]] static auto adopt(CGEventRef ref) noexcept -> event {
    return event{cf::adopt(ref)};
  }

  /// Retains an event view reference with `CFRetain`.
  [[nodiscard]] static auto retain(event_view view) noexcept -> event {
    return event{cf::retain(view.get())};
  }

  /// Creates a new Quartz event with `CGEventCreate`.
  [[nodiscard]] static auto create(event_source_view source = {}) noexcept -> event {
    return adopt(CGEventCreate(source.get()));
  }

  /// Creates a mouse event with `CGEventCreateMouseEvent`.
  [[nodiscard]] static auto create_mouse(
      event_source_view source,
      CGEventType type,
      CGPoint cursor_position,
      CGMouseButton button) noexcept -> event {
    return adopt(CGEventCreateMouseEvent(
        source.get(), type, cursor_position, button));
  }

  /// Creates a keyboard event with `CGEventCreateKeyboardEvent`.
  [[nodiscard]] static auto create_keyboard(
      event_source_view source,
      CGKeyCode key_code,
      bool key_down) noexcept -> event {
    return adopt(CGEventCreateKeyboardEvent(source.get(), key_code, key_down));
  }

  /// Copies an event with `CGEventCreateCopy`.
  [[nodiscard]] static auto copy(event_view value) noexcept -> event {
    return adopt(CGEventCreateCopy(value.get()));
  }

  /// Reconstructs an event with `CGEventCreateFromData`.
  [[nodiscard]] static auto from_data(cf::view<CFDataRef> data) noexcept -> event {
    return adopt(CGEventCreateFromData(kCFAllocatorDefault, data.get()));
  }

  /// Creates a matching event source with `CGEventCreateSourceFromEvent`.
  [[nodiscard]] static auto create_source_from_event(event_view value) noexcept
      -> event_source;

  /// Returns the wrapped raw event reference.
  [[nodiscard]] auto get() const noexcept -> CGEventRef {
    return ref_.get();
  }

  /// Returns whether the wrapper owns an event.
  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(ref_);
  }

  /// Returns a non-owning view of the wrapped event.
  [[nodiscard]] auto view() const noexcept -> event_view {
    return event_view{get()};
  }

  /// Returns the event type via `CGEventGetType`.
  [[nodiscard]] auto type() const noexcept -> CGEventType {
    return view().type();
  }

  /// Updates the event type via `CGEventSetType`.
  void set_type(CGEventType type) const noexcept {
    view().set_type(type);
  }

  /// Returns the event timestamp via `CGEventGetTimestamp`.
  [[nodiscard]] auto timestamp() const noexcept -> CGEventTimestamp {
    return view().timestamp();
  }

  /// Updates the event timestamp via `CGEventSetTimestamp`.
  void set_timestamp(CGEventTimestamp timestamp) const noexcept {
    view().set_timestamp(timestamp);
  }

  /// Returns the event location via `CGEventGetLocation`.
  [[nodiscard]] auto location() const noexcept -> CGPoint {
    return view().location();
  }

  /// Updates the event location via `CGEventSetLocation`.
  void set_location(CGPoint location) const noexcept {
    view().set_location(location);
  }

  /// Returns the modifier flags via `CGEventGetFlags`.
  [[nodiscard]] auto flags() const noexcept -> CGEventFlags {
    return view().flags();
  }

  /// Updates the modifier flags via `CGEventSetFlags`.
  void set_flags(CGEventFlags flags) const noexcept {
    view().set_flags(flags);
  }

  /// Returns an integer event field via `CGEventGetIntegerValueField`.
  [[nodiscard]] auto integer_field(CGEventField field) const noexcept -> std::int64_t {
    return view().integer_field(field);
  }

  /// Updates an integer event field via `CGEventSetIntegerValueField`.
  void set_integer_field(CGEventField field, std::int64_t value) const noexcept {
    view().set_integer_field(field, value);
  }

  /// Returns a floating-point event field via `CGEventGetDoubleValueField`.
  [[nodiscard]] auto double_field(CGEventField field) const noexcept -> double {
    return view().double_field(field);
  }

  /// Updates a floating-point event field via `CGEventSetDoubleValueField`.
  void set_double_field(CGEventField field, double value) const noexcept {
    view().set_double_field(field, value);
  }

  /// Serializes the event with `CGEventCreateData`.
  [[nodiscard]] auto create_data() const noexcept -> cf::retained<CFDataRef> {
    return view().create_data();
  }

  /// Posts the event with `CGEventPost`.
  void post(CGEventTapLocation tap_location) const noexcept {
    view().post(tap_location);
  }

  /// Posts the event to a process with `CGEventPostToPid`.
  void post_to_pid(pid_t pid) const noexcept {
    view().post_to_pid(pid);
  }

 private:
  explicit event(cf::retained<CGEventRef> ref) noexcept : ref_(std::move(ref)) {}

  cf::retained<CGEventRef> ref_{};
};

/// Posts an event through an active tap proxy via `CGEventTapPostEvent`.
inline void post_tap_event(CGEventTapProxy proxy, event_view value) noexcept {
  CGEventTapPostEvent(proxy, value.get());
}

/// Warps the cursor position via `CGWarpMouseCursorPosition`.
[[nodiscard]] inline auto warp_mouse_cursor_position(CGPoint position) noexcept -> CGError {
  return CGWarpMouseCursorPosition(position);
}

/// Hides the cursor via `CGDisplayHideCursor` (balanced by `show_cursor`).
[[nodiscard]] inline auto hide_cursor() noexcept -> CGError {
  return CGDisplayHideCursor(kCGDirectMainDisplay);
}

/// Shows the cursor via `CGDisplayShowCursor`.
[[nodiscard]] inline auto show_cursor() noexcept -> CGError {
  return CGDisplayShowCursor(kCGDirectMainDisplay);
}

/// Returns how many events of `type` the HID system state has seen, via
/// `CGEventSourceCounterForEventType`. Events posted to the HID tap count once
/// the window server has applied them, so a change means "processed".
[[nodiscard]] inline auto hid_event_count(CGEventType type) noexcept -> std::uint32_t {
  return CGEventSourceCounterForEventType(kCGEventSourceStateHIDSystemState, type);
}

/// Fills the current active-display list via `CGGetActiveDisplayList`.
inline auto active_displays(std::vector<CGDirectDisplayID> &displays) noexcept -> CGError {
  constexpr std::uint32_t buffer_size = 32;
  displays.assign(buffer_size, CGDirectDisplayID{});

  std::uint32_t count = 0;
  const CGError error =
      CGGetActiveDisplayList(buffer_size, displays.data(), &count);
  if (error != kCGErrorSuccess) {
    displays.clear();
    return error;
  }

  displays.resize(count);
  return error;
}

/// Fills the displays containing a point via `CGGetDisplaysWithPoint`.
inline auto displays_with_point(
    CGPoint point,
    std::vector<CGDirectDisplayID> &displays) noexcept -> CGError {
  constexpr std::uint32_t buffer_size = 32;
  displays.assign(buffer_size, CGDirectDisplayID{});

  std::uint32_t count = 0;
  const CGError error =
      CGGetDisplaysWithPoint(point, buffer_size, displays.data(), &count);
  if (error != kCGErrorSuccess) {
    displays.clear();
    return error;
  }

  displays.resize(count);
  return error;
}

/// Returns the bounds of a display via `CGDisplayBounds`.
[[nodiscard]] inline auto display_bounds(CGDirectDisplayID display) noexcept -> CGRect {
  return CGDisplayBounds(display);
}

/// Returns the stable UUID string for a display via `CGDisplayCreateUUIDFromDisplayID`.
[[nodiscard]] inline auto display_uuid_string(CGDirectDisplayID display) noexcept
    -> cf::string {
  const auto uuid = cf::adopt(CGDisplayCreateUUIDFromDisplayID(display));
  return cf::uuid_create_string(cf::view{uuid.get()});
}

/// Copies the current window list via `CGWindowListCopyWindowInfo`.
[[nodiscard]] inline auto copy_window_info(
    CGWindowListOption option,
    CGWindowID relative_to_window) noexcept -> cf::retained<CFArrayRef> {
  return cf::adopt(CGWindowListCopyWindowInfo(option, relative_to_window));
}

/// Builds an owning event-source wrapper with `CGEventCreateSourceFromEvent`.
inline auto event::create_source_from_event(event_view value) noexcept -> event_source {
  return event_source::adopt(CGEventCreateSourceFromEvent(value.get()));
}

}  // namespace spacehound::cg
