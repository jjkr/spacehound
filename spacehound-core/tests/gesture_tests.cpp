// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <ApplicationServices/ApplicationServices.h>

#include <cstddef>
#include <cstdint>
#include <span>

#include <spacehound/gesture.hpp>

namespace {

namespace cg = spacehound::cg;
namespace gesture = spacehound::gesture;

auto make_event() -> cg::event {
  return cg::event::adopt(CGEventCreate(nullptr));
}

TEST(gesture_tests, swipe_options_defaults_match_fast_swipe_behavior) {
  const gesture::swipe_options options{};

  EXPECT_EQ(options.finger_count, 3);
  EXPECT_DOUBLE_EQ(options.update_delta, 0.0001);
  EXPECT_DOUBLE_EQ(options.end_delta, 1.0);
  EXPECT_DOUBLE_EQ(options.momentum, 29100.0);
}

TEST(gesture_tests, populate_swipe_event_sets_expected_begin_phase_fields) {
  const auto event = make_event();
  ASSERT_TRUE(event);

  gesture::populate_swipe_event(
      event.view(), gesture::phase::begin, gesture::direction::right);

  EXPECT_EQ(event.type(), cg::gesture_event_type);
  EXPECT_EQ(
      event.integer_field(cg::event_type_field),
      static_cast<std::int64_t>(cg::gesture_event_type));
  EXPECT_EQ(event.integer_field(cg::gesture_subtype_field), 0x17);
  EXPECT_EQ(
      event.integer_field(cg::gesture_phase_field),
      static_cast<std::int64_t>(gesture::phase::begin));
  EXPECT_EQ(
      event.integer_field(cg::gesture_phase_mirror_field),
      static_cast<std::int64_t>(gesture::phase::begin));
  EXPECT_EQ(event.integer_field(cg::gesture_finger_count_field), 3);
  EXPECT_EQ(event.integer_field(cg::gesture_type_field), 1);
  EXPECT_EQ(event.integer_field(cg::gesture_type_mirror_field), 1);
  EXPECT_NEAR(event.double_field(cg::gesture_delta_field), 0.0001, 1e-6);
}

TEST(gesture_tests, populate_swipe_event_sets_expected_update_phase_fields) {
  const auto event = make_event();
  ASSERT_TRUE(event);

  gesture::populate_swipe_event(
      event.view(), gesture::phase::update, gesture::direction::up);

  EXPECT_EQ(
      event.integer_field(cg::gesture_phase_field),
      static_cast<std::int64_t>(gesture::phase::update));
  EXPECT_EQ(
      event.integer_field(cg::gesture_phase_mirror_field),
      static_cast<std::int64_t>(gesture::phase::update));
  EXPECT_EQ(event.integer_field(cg::gesture_type_field), 2);
  EXPECT_EQ(event.integer_field(cg::gesture_type_mirror_field), 2);
  EXPECT_NEAR(event.double_field(cg::gesture_delta_field), 0.0001, 1e-6);
}

TEST(gesture_tests, populate_swipe_event_sets_expected_end_phase_fields) {
  const auto event = make_event();
  ASSERT_TRUE(event);

  gesture::populate_swipe_event(
      event.view(), gesture::phase::end, gesture::direction::right);

  EXPECT_EQ(
      event.integer_field(cg::gesture_phase_field),
      static_cast<std::int64_t>(gesture::phase::end));
  EXPECT_NEAR(event.double_field(cg::gesture_delta_field), 1.0, 1e-6);
  EXPECT_NEAR(event.double_field(cg::gesture_momentum_x_field), 29100.0, 1e-3);
  EXPECT_NEAR(event.double_field(cg::gesture_momentum_y_field), 29100.0, 1e-3);
}

TEST(gesture_tests, populate_swipe_event_maps_direction_signs_and_gesture_types) {
  const struct {
    gesture::direction direction;
    std::int64_t gesture_type;
    double sign;
  } cases[] = {
      {gesture::direction::left, 1, -1.0},
      {gesture::direction::right, 1, 1.0},
      {gesture::direction::up, 2, 1.0},
      {gesture::direction::down, 2, -1.0},
  };

  for (const auto &test_case : cases) {
    const auto event = make_event();
    ASSERT_TRUE(event);

    gesture::populate_swipe_event(
        event.view(), gesture::phase::end, test_case.direction);

    EXPECT_EQ(event.integer_field(cg::gesture_type_field), test_case.gesture_type);
    EXPECT_EQ(
        event.integer_field(cg::gesture_type_mirror_field),
        test_case.gesture_type);
    EXPECT_NEAR(event.double_field(cg::gesture_delta_field), test_case.sign, 1e-6);
    EXPECT_NEAR(
        event.double_field(cg::gesture_momentum_x_field),
        29100.0 * test_case.sign,
        1e-3);
    EXPECT_NEAR(
        event.double_field(cg::gesture_momentum_y_field),
        29100.0 * test_case.sign,
        1e-3);
  }
}

TEST(gesture_tests, populate_swipe_event_uses_custom_swipe_options) {
  const auto event = make_event();
  ASSERT_TRUE(event);

  const gesture::swipe_options options{
      .finger_count = 4,
      .update_delta = 0.25,
      .end_delta = 2.5,
      .momentum = 99.0,
  };

  gesture::populate_swipe_event(
      event.view(), gesture::phase::update, gesture::direction::left, options);
  EXPECT_EQ(event.integer_field(cg::gesture_finger_count_field), 4);
  EXPECT_NEAR(event.double_field(cg::gesture_delta_field), -0.25, 1e-6);

  gesture::populate_swipe_event(
      event.view(), gesture::phase::end, gesture::direction::left, options);
  EXPECT_NEAR(event.double_field(cg::gesture_delta_field), -2.5, 1e-6);
  EXPECT_NEAR(event.double_field(cg::gesture_momentum_x_field), -99.0, 1e-6);
  EXPECT_NEAR(event.double_field(cg::gesture_momentum_y_field), -99.0, 1e-6);
}

TEST(gesture_tests, create_swipe_event_returns_populated_event) {
  const auto event = gesture::create_swipe_event(
      cg::event_source_view{},
      gesture::phase::update,
      gesture::direction::down);
  ASSERT_TRUE(event);

  EXPECT_EQ(event.type(), cg::gesture_event_type);
  EXPECT_EQ(
      event.integer_field(cg::gesture_phase_field),
      static_cast<std::int64_t>(gesture::phase::update));
  EXPECT_EQ(event.integer_field(cg::gesture_type_field), 2);
  EXPECT_NEAR(event.double_field(cg::gesture_delta_field), -0.0001, 1e-6);
}

TEST(gesture_tests, create_swipe_event_reports_requested_location_after_reconstruction) {
  const gesture::swipe_options options{.location = CGPointMake(-3200.0, 480.0)};
  for (const auto phase : {gesture::phase::begin, gesture::phase::update, gesture::phase::end}) {
    const auto event = gesture::create_swipe_event(
        cg::event_source_view{}, phase, gesture::direction::right, options);
    ASSERT_TRUE(event);

    const auto location = event.location();
    EXPECT_DOUBLE_EQ(location.x, -3200.0);
    EXPECT_DOUBLE_EQ(location.y, 480.0);
  }
}

TEST(gesture_tests, create_swipe_event_embeds_raw_gesture_payload) {
  const auto begin_event = gesture::create_swipe_event(
      cg::event_source_view{},
      gesture::phase::begin,
      gesture::direction::right);
  ASSERT_TRUE(begin_event);

  const auto begin_data = begin_event.create_data();
  ASSERT_TRUE(begin_data);
  const auto begin_bytes = std::span<const std::uint8_t>{
      CFDataGetBytePtr(begin_data.get()),
      static_cast<std::size_t>(CFDataGetLength(begin_data.get())),
  };
  std::size_t begin_offset = 0;
  ASSERT_TRUE(gesture::detail::find_serialized_field_tag(
      begin_bytes,
      gesture::detail::raw_gesture_field_id,
      begin_offset));
  EXPECT_EQ(gesture::detail::read_u16_be(begin_bytes.data() + begin_offset), 68U);

  const auto end_event = gesture::create_swipe_event(
      cg::event_source_view{},
      gesture::phase::end,
      gesture::direction::left);
  ASSERT_TRUE(end_event);

  const auto end_data = end_event.create_data();
  ASSERT_TRUE(end_data);
  const auto end_bytes = std::span<const std::uint8_t>{
      CFDataGetBytePtr(end_data.get()),
      static_cast<std::size_t>(CFDataGetLength(end_data.get())),
  };
  std::size_t end_offset = 0;
  ASSERT_TRUE(gesture::detail::find_serialized_field_tag(
      end_bytes,
      gesture::detail::raw_gesture_field_id,
      end_offset));
  EXPECT_EQ(gesture::detail::read_u16_be(end_bytes.data() + end_offset), 96U);
}

TEST(gesture_tests, reconstructed_swipe_preserves_source_user_data) {
  constexpr std::int64_t marker = 0x5348544150494E47LL;
  const auto seed_event = make_event();
  ASSERT_TRUE(seed_event);
  gesture::populate_swipe_event(
      seed_event.view(), gesture::phase::begin, gesture::direction::right);
  seed_event.set_integer_field(kCGEventSourceUserData, marker);

  const auto event = gesture::detail::add_raw_gesture_payload(
      seed_event.view(),
      gesture::phase::begin,
      gesture::direction::right,
      gesture::swipe_options{});
  ASSERT_TRUE(event);
  EXPECT_EQ(event.integer_field(kCGEventSourceUserData), marker);
}

}  // namespace
