#pragma once

#include <ApplicationServices/ApplicationServices.h>

#include <cstdint>

#include <spacerabbit/cg.hpp>

namespace spacerabbit::gesture {

/// Cardinal directions supported by SpaceRabbit synthetic swipe gestures.
enum class direction {
  left,
  right,
  up,
  down,
};

/// Gesture phases used by the private Quartz swipe event sequence.
enum class phase : std::int64_t {
  begin = 1,
  update = 2,
  end = 4,
};

/// Tuning parameters for synthetic swipe gesture construction.
struct swipe_options final {
  std::int64_t finger_count = 3;
  double update_delta = 0.0001;
  double end_delta = 1.0;
  double momentum = 29100.0;
};

namespace detail {

[[nodiscard]] inline auto direction_sign(direction value) noexcept -> double {
  switch (value) {
    case direction::right:
    case direction::up:
      return 1.0;
    case direction::left:
    case direction::down:
      return -1.0;
  }

  return 0.0;
}

[[nodiscard]] inline auto gesture_type(direction value) noexcept -> std::int64_t {
  switch (value) {
    case direction::left:
    case direction::right:
      return 1;
    case direction::up:
    case direction::down:
      return 2;
  }

  return 0;
}

}  // namespace detail

/// Populates an existing Quartz event with the private fields used for swipe synthesis.
inline void populate_swipe_event(
    cg::event_view event,
    phase gesture_phase,
    direction swipe_direction,
    const swipe_options &options = {}) noexcept {
  if (!event) {
    return;
  }

  const auto gesture_kind = detail::gesture_type(swipe_direction);
  const auto sign = detail::direction_sign(swipe_direction);

  event.set_type(cg::gesture_event_type);
  event.set_integer_field(
      cg::event_type_field, static_cast<std::int64_t>(cg::gesture_event_type));
  event.set_integer_field(cg::gesture_subtype_field, 0x17);
  event.set_integer_field(
      cg::gesture_phase_field, static_cast<std::int64_t>(gesture_phase));
  event.set_integer_field(
      cg::gesture_phase_mirror_field, static_cast<std::int64_t>(gesture_phase));
  event.set_integer_field(cg::gesture_finger_count_field, options.finger_count);
  event.set_integer_field(cg::gesture_type_field, gesture_kind);
  event.set_integer_field(cg::gesture_type_mirror_field, gesture_kind);

  if (gesture_phase == phase::end) {
    event.set_double_field(cg::gesture_delta_field, options.end_delta * sign);
    event.set_double_field(
        cg::gesture_momentum_x_field, options.momentum * sign);
    event.set_double_field(
        cg::gesture_momentum_y_field, options.momentum * sign);
    return;
  }

  event.set_double_field(cg::gesture_delta_field, options.update_delta * sign);
}

/// Creates a new synthetic swipe event for the requested phase and direction.
[[nodiscard]] inline auto create_swipe_event(
    cg::event_source_view source,
    phase gesture_phase,
    direction swipe_direction,
    const swipe_options &options = {}) noexcept -> cg::event {
  auto event = cg::event::create(source);
  if (!event) {
    return {};
  }

  populate_swipe_event(event.view(), gesture_phase, swipe_direction, options);
  return event;
}

/// Posts a complete begin/update/end swipe sequence to the supplied Quartz tap location.
[[nodiscard]] inline auto post_swipe(
    cg::event_source_view source,
    direction swipe_direction,
    CGEventTapLocation tap_location = kCGHIDEventTap,
    const swipe_options &options = {}) noexcept -> bool {
  auto begin_event = create_swipe_event(source, phase::begin, swipe_direction, options);
  if (!begin_event) {
    return false;
  }

  auto update_event =
      create_swipe_event(source, phase::update, swipe_direction, options);
  if (!update_event) {
    return false;
  }

  auto end_event = create_swipe_event(source, phase::end, swipe_direction, options);
  if (!end_event) {
    return false;
  }

  begin_event.post(tap_location);
  update_event.post(tap_location);
  end_event.post(tap_location);
  return true;
}

/// Posts a complete begin/update/end swipe sequence through an active event tap proxy.
[[nodiscard]] inline auto post_swipe(
    CGEventTapProxy proxy,
    cg::event_source_view source,
    direction swipe_direction,
    const swipe_options &options = {}) noexcept -> bool {
  auto begin_event = create_swipe_event(source, phase::begin, swipe_direction, options);
  if (!begin_event) {
    return false;
  }

  auto update_event =
      create_swipe_event(source, phase::update, swipe_direction, options);
  if (!update_event) {
    return false;
  }

  auto end_event = create_swipe_event(source, phase::end, swipe_direction, options);
  if (!end_event) {
    return false;
  }

  cg::post_tap_event(proxy, begin_event.view());
  cg::post_tap_event(proxy, update_event.view());
  cg::post_tap_event(proxy, end_event.view());
  return true;
}

}  // namespace spacerabbit::gesture
