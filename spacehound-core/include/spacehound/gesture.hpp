// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <ApplicationServices/ApplicationServices.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include <spacehound/cg.hpp>

namespace spacehound::gesture {

/// Cardinal directions supported by SpaceHound synthetic swipe gestures.
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
  /// Where the gesture is reported to happen. The Dock switches Spaces on the
  /// display containing this point, so setting it targets a display without
  /// moving the cursor. Defaults to the current cursor location.
  std::optional<CGPoint> location;
};

namespace detail {

inline constexpr std::uint16_t raw_gesture_field_id = 4205;

inline void write_u16_be(std::uint8_t *bytes, std::uint16_t value) noexcept {
  bytes[0] = static_cast<std::uint8_t>(value >> 8);
  bytes[1] = static_cast<std::uint8_t>(value);
}

inline void write_u16_le(std::uint8_t *bytes, std::uint16_t value) noexcept {
  bytes[0] = static_cast<std::uint8_t>(value);
  bytes[1] = static_cast<std::uint8_t>(value >> 8);
}

inline void write_u32_le(std::uint8_t *bytes, std::uint32_t value) noexcept {
  bytes[0] = static_cast<std::uint8_t>(value);
  bytes[1] = static_cast<std::uint8_t>(value >> 8);
  bytes[2] = static_cast<std::uint8_t>(value >> 16);
  bytes[3] = static_cast<std::uint8_t>(value >> 24);
}

[[nodiscard]] inline auto read_u16_be(const std::uint8_t *bytes) noexcept
    -> std::uint16_t {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(bytes[0]) << 8) |
      static_cast<std::uint16_t>(bytes[1]));
}

[[nodiscard]] inline auto serialized_payload_length(
    std::uint16_t size_value,
    std::uint16_t value_type) noexcept -> std::size_t {
  switch (value_type) {
    case 0:
      return size_value == 1
                 ? 8
                 : (static_cast<std::size_t>(size_value) + 3U) &
                       ~std::size_t{3};
    case 1:
    case 3:
      return static_cast<std::size_t>(size_value) * 4U;
    default:
      return 0;
  }
}

[[nodiscard]] inline auto find_serialized_field_tag(
    std::span<const std::uint8_t> bytes,
    std::uint16_t wanted_field,
    std::size_t &out_offset) noexcept -> bool {
  std::size_t offset = 4;

  while (offset + 4U <= bytes.size()) {
    const auto size_value = read_u16_be(bytes.data() + offset);
    const auto type_and_field = read_u16_be(bytes.data() + offset + 2U);
    const auto value_type = static_cast<std::uint16_t>(type_and_field >> 14);
    const auto field_id = static_cast<std::uint16_t>(type_and_field & 0x3fffU);
    const auto payload_length =
        serialized_payload_length(size_value, value_type);

    if (field_id == wanted_field) {
      out_offset = offset;
      return true;
    }

    if (payload_length == 0 || payload_length > bytes.size() - offset - 4U) {
      return false;
    }

    offset += 4U + payload_length;
  }

  return false;
}

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

[[nodiscard]] inline auto fixed_16_16(double value) noexcept -> std::int32_t {
  return static_cast<std::int32_t>(std::llround(value * 65536.0));
}

[[nodiscard]] inline auto make_raw_gesture_payload(
    phase gesture_phase,
    direction swipe_direction,
    const swipe_options &options) -> std::vector<std::uint8_t> {
  const bool is_end = gesture_phase == phase::end;
  std::vector<std::uint8_t> payload(is_end ? 96U : 68U);
  const auto sign = direction_sign(swipe_direction);
  const auto progress = fixed_16_16(
      (is_end ? options.end_delta : options.update_delta) * sign);

  // IOHIDSystemQueueElement. Sender ID and timestamp are intentionally zero.
  write_u32_le(payload.data() + 24U, is_end ? 2U : 1U);

  // IOHIDFluidTouchGestureData.
  write_u32_le(payload.data() + 28U, 40U);
  write_u32_le(payload.data() + 32U, 23U);
  write_u32_le(
      payload.data() + 36U,
      static_cast<std::uint32_t>(gesture_phase) << 24);
  write_u16_le(
      payload.data() + 60U,
      static_cast<std::uint16_t>(gesture_type(swipe_direction)));
  write_u16_le(payload.data() + 62U, 3U);
  write_u32_le(payload.data() + 64U, static_cast<std::uint32_t>(progress));

  if (is_end) {
    const auto velocity = fixed_16_16(options.momentum * sign);

    // IOHIDVelocityEventData.
    write_u32_le(payload.data() + 68U, 28U);
    write_u32_le(payload.data() + 72U, 9U);
    payload[80] = 1U;
    write_u32_le(payload.data() + 84U, static_cast<std::uint32_t>(velocity));
    write_u32_le(payload.data() + 88U, static_cast<std::uint32_t>(velocity));
  }

  return payload;
}

[[nodiscard]] inline auto add_raw_gesture_payload(
    cg::event_view seed_event,
    phase gesture_phase,
    direction swipe_direction,
    const swipe_options &options) noexcept -> cg::event {
  const auto source_user_data =
      seed_event.integer_field(kCGEventSourceUserData);
  const auto serialized = seed_event.create_data();
  if (!serialized) {
    return {};
  }

  const auto serialized_length = CFDataGetLength(serialized.get());
  if (serialized_length <= 0) {
    return {};
  }

  const auto original = std::span<const std::uint8_t>{
      CFDataGetBytePtr(serialized.get()),
      static_cast<std::size_t>(serialized_length),
  };
  std::size_t insertion_offset = 0;
  if (!find_serialized_field_tag(original, 110U, insertion_offset)) {
    return {};
  }

  const auto payload =
      make_raw_gesture_payload(gesture_phase, swipe_direction, options);
  std::vector<std::uint8_t> result(original.size() + 4U + payload.size());

  std::memcpy(result.data(), original.data(), insertion_offset);
  write_u16_be(
      result.data() + insertion_offset,
      static_cast<std::uint16_t>(payload.size()));
  write_u16_be(result.data() + insertion_offset + 2U, raw_gesture_field_id);
  std::memcpy(
      result.data() + insertion_offset + 4U,
      payload.data(),
      payload.size());
  std::memcpy(
      result.data() + insertion_offset + 4U + payload.size(),
      original.data() + insertion_offset,
      original.size() - insertion_offset);

  const auto data = cf::adopt(CFDataCreate(
      kCFAllocatorDefault,
      result.data(),
      static_cast<CFIndex>(result.size())));
  if (!data) {
    return {};
  }

  auto event = cg::event::from_data(cf::view<CFDataRef>{data.get()});
  if (event) {
    event.set_integer_field(kCGEventSourceUserData, source_user_data);
  }
  return event;
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

  // Must precede the raw payload step, which re-creates the event from its
  // serialized form.
  if (options.location) {
    event.set_location(*options.location);
  }

  populate_swipe_event(event.view(), gesture_phase, swipe_direction, options);
  return detail::add_raw_gesture_payload(
      event.view(), gesture_phase, swipe_direction, options);
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

}  // namespace spacehound::gesture
