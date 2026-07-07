#include <gtest/gtest.h>

#include <ApplicationServices/ApplicationServices.h>

#include <cstdint>

#include <spacerabbit/gesture.hpp>

namespace {

namespace cg = spacerabbit::cg;
namespace gesture = spacerabbit::gesture;

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

}  // namespace
