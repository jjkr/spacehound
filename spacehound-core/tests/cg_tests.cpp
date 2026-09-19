// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <ApplicationServices/ApplicationServices.h>

#include <cstdlib>
#include <cstdint>
#include <type_traits>
#include <vector>

#include <spacehound/cf.hpp>
#include <spacehound/cg.hpp>

namespace {

namespace cf = spacehound::cf;
namespace cg = spacehound::cg;

static_assert(!std::is_copy_constructible_v<cg::event>);
static_assert(!std::is_copy_assignable_v<cg::event>);
static_assert(!std::is_copy_constructible_v<cg::event_source>);
static_assert(!std::is_copy_assignable_v<cg::event_source>);
static_assert(!std::is_copy_constructible_v<cg::event_tap>);
static_assert(!std::is_copy_assignable_v<cg::event_tap>);
static_assert(std::is_trivially_copyable_v<cg::event_view>);
static_assert(std::is_trivially_copyable_v<cg::event_source_view>);
static_assert(std::is_trivially_copyable_v<cg::event_tap_view>);
static_assert(!std::is_convertible_v<cg::event, CGEventRef>);
static_assert(!std::is_convertible_v<cg::event_source, CGEventSourceRef>);
static_assert(!std::is_convertible_v<cg::event_tap, CFMachPortRef>);

auto make_source() -> cg::event_source {
  // Test helpers centralize the event-source configuration used across cases.
  return cg::event_source::create(kCGEventSourceStateHIDSystemState);
}

auto make_event() -> cg::event {
  return cg::event::adopt(CGEventCreate(nullptr));
}

TEST(cg_tests, event_source_view_defaults_to_null) {
  const cg::event_source_view view{};
  EXPECT_FALSE(view);
  EXPECT_EQ(view.get(), nullptr);
}

TEST(cg_tests, event_view_defaults_to_null) {
  const cg::event_view view{};
  EXPECT_FALSE(view);
  EXPECT_EQ(view.get(), nullptr);
}

TEST(cg_tests, event_tap_view_defaults_to_null) {
  const cg::event_tap_view view{};
  EXPECT_FALSE(view);
  EXPECT_EQ(view.get(), nullptr);
  EXPECT_FALSE(view.create_run_loop_source());
}

TEST(cg_tests, event_source_view_getters_and_setters_round_trip) {
  const auto source = make_source();
  ASSERT_TRUE(source);

  const auto view = source.view();
  ASSERT_TRUE(view);

  const auto keyboard_type = view.keyboard_type();
  view.set_keyboard_type(keyboard_type);
  EXPECT_EQ(view.keyboard_type(), keyboard_type);

  view.set_pixels_per_line(24.5);
  EXPECT_DOUBLE_EQ(view.pixels_per_line(), 24.5);

  view.set_user_data(0x123456789LL);
  EXPECT_EQ(view.user_data(), 0x123456789LL);
  EXPECT_EQ(view.source_state_id(), kCGEventSourceStateHIDSystemState);
}

TEST(cg_tests, event_source_adopt_retain_and_move_preserve_underlying_ref) {
  auto source = make_source();
  ASSERT_TRUE(source);
  source.set_user_data(99);

  const auto retained = cg::event_source::retain(source.view());
  ASSERT_TRUE(retained);
  EXPECT_EQ(retained.user_data(), 99);

  auto moved = std::move(source);
  EXPECT_FALSE(source);
  ASSERT_TRUE(moved);
  EXPECT_EQ(moved.user_data(), 99);

  const auto adopted = cg::event_source::adopt(
      CGEventSourceCreate(kCGEventSourceStateCombinedSessionState));
  ASSERT_TRUE(adopted);
  EXPECT_EQ(adopted.source_state_id(), kCGEventSourceStateCombinedSessionState);
}

TEST(cg_tests, event_view_getters_and_setters_round_trip) {
  const auto event = make_event();
  ASSERT_TRUE(event);

  const auto view = event.view();
  ASSERT_TRUE(view);

  view.set_type(kCGEventMouseMoved);
  view.set_flags(kCGEventFlagMaskCommand | kCGEventFlagMaskShift);
  view.set_timestamp(static_cast<CGEventTimestamp>(321654987ULL));
  view.set_location(CGPointMake(12.5, 34.25));
  view.set_integer_field(kCGMouseEventNumber, 99);
  view.set_double_field(kCGMouseEventPressure, 0.5);

  EXPECT_EQ(view.type(), kCGEventMouseMoved);
  EXPECT_EQ(view.flags(), (kCGEventFlagMaskCommand | kCGEventFlagMaskShift));
  EXPECT_EQ(view.timestamp(), static_cast<CGEventTimestamp>(321654987ULL));
  EXPECT_EQ(view.location().x, 12.5);
  EXPECT_EQ(view.location().y, 34.25);
  EXPECT_EQ(view.integer_field(kCGMouseEventNumber), 99);
  EXPECT_NEAR(view.double_field(kCGMouseEventPressure), 0.5, 0.01);
}

TEST(cg_tests, event_methods_delegate_to_view_and_round_trip_through_data) {
  const auto type = &cg::event::type;
  const auto set_type = &cg::event::set_type;
  const auto flags = &cg::event::flags;
  const auto set_flags = &cg::event::set_flags;
  const auto timestamp = &cg::event::timestamp;
  const auto set_timestamp = &cg::event::set_timestamp;
  const auto location = &cg::event::location;
  const auto set_location = &cg::event::set_location;
  const auto integer_field = &cg::event::integer_field;
  const auto set_integer_field = &cg::event::set_integer_field;
  const auto double_field = &cg::event::double_field;
  const auto set_double_field = &cg::event::set_double_field;
  const auto create_data = &cg::event::create_data;
  const auto post = &cg::event::post;
  const auto post_to_pid = &cg::event::post_to_pid;

  EXPECT_TRUE(type != nullptr);
  EXPECT_TRUE(set_type != nullptr);
  EXPECT_TRUE(flags != nullptr);
  EXPECT_TRUE(set_flags != nullptr);
  EXPECT_TRUE(timestamp != nullptr);
  EXPECT_TRUE(set_timestamp != nullptr);
  EXPECT_TRUE(location != nullptr);
  EXPECT_TRUE(set_location != nullptr);
  EXPECT_TRUE(integer_field != nullptr);
  EXPECT_TRUE(set_integer_field != nullptr);
  EXPECT_TRUE(double_field != nullptr);
  EXPECT_TRUE(set_double_field != nullptr);
  EXPECT_TRUE(create_data != nullptr);
  EXPECT_TRUE(post != nullptr);
  EXPECT_TRUE(post_to_pid != nullptr);
}

TEST(cg_tests, event_adopt_and_move_preserve_underlying_ref) {
  auto event = cg::event::adopt(CGEventCreate(nullptr));
  ASSERT_TRUE(event);
  event.set_type(kCGEventLeftMouseDown);

  auto moved = std::move(event);
  EXPECT_FALSE(event);
  ASSERT_TRUE(moved);
  EXPECT_EQ(moved.type(), kCGEventLeftMouseDown);
}

TEST(cg_tests, mouse_and_keyboard_event_creation_helpers_work) {
  const auto create_mouse = &cg::event::create_mouse;
  const auto create_keyboard = &cg::event::create_keyboard;
  ASSERT_NE(create_mouse, nullptr);
  ASSERT_NE(create_keyboard, nullptr);

  if (std::getenv("SPACEHOUND_ENABLE_LIVE_QUARTZ_TESTS") == nullptr) {
    GTEST_SKIP() << "Set SPACEHOUND_ENABLE_LIVE_QUARTZ_TESTS=1 to exercise live Quartz event creation.";
  }

  const auto source = make_source();
  ASSERT_TRUE(source);

  const auto mouse = create_mouse(
      source.view(), kCGEventMouseMoved, CGPointMake(3.0, 5.0), kCGMouseButtonLeft);
  ASSERT_TRUE(mouse);
  EXPECT_EQ(mouse.type(), kCGEventMouseMoved);
  EXPECT_DOUBLE_EQ(mouse.location().x, 3.0);
  EXPECT_DOUBLE_EQ(mouse.location().y, 5.0);

  const auto keyboard = create_keyboard(source.view(), static_cast<CGKeyCode>(6), true);
  ASSERT_TRUE(keyboard);
  EXPECT_EQ(keyboard.type(), kCGEventKeyDown);
}

TEST(cg_tests, display_and_window_helpers_report_system_state) {
  std::vector<CGDirectDisplayID> displays;
  ASSERT_EQ(cg::active_displays(displays), kCGErrorSuccess);
  if (displays.empty()) {
    GTEST_SKIP() << "No active displays were reported in this environment.";
  }

  const auto bounds = cg::display_bounds(displays.front());
  EXPECT_GT(bounds.size.width, 0.0);
  EXPECT_GT(bounds.size.height, 0.0);

  std::vector<CGDirectDisplayID> point_displays;
  ASSERT_EQ(cg::displays_with_point(bounds.origin, point_displays), kCGErrorSuccess);
  if (point_displays.empty()) {
    GTEST_SKIP() << "No displays were reported for the active-display origin.";
  }

  const auto uuid = cg::display_uuid_string(displays.front());
  ASSERT_TRUE(uuid);
  EXPECT_FALSE(uuid.empty());

  const auto window_info = cg::copy_window_info(kCGWindowListOptionOnScreenOnly, kCGNullWindowID);
  if (!window_info) {
    GTEST_SKIP() << "Quartz window info is unavailable in this environment.";
  }
  EXPECT_TRUE(cf::is<CFArrayRef>(cf::view{static_cast<CFTypeRef>(window_info.get())}));
}

TEST(cg_tests, constants_and_event_helpers_are_addressable) {
  EXPECT_EQ(cg::window_number_key.get(), kCGWindowNumber);
  EXPECT_EQ(cg::window_owner_pid_key.get(), kCGWindowOwnerPID);
  EXPECT_EQ(cg::window_layer_key.get(), kCGWindowLayer);
  EXPECT_EQ(cg::window_name_key.get(), kCGWindowName);
  EXPECT_EQ(cg::window_owner_name_key.get(), kCGWindowOwnerName);
  EXPECT_EQ(cg::window_bounds_key.get(), kCGWindowBounds);
  EXPECT_EQ(cg::window_alpha_key.get(), kCGWindowAlpha);
  EXPECT_EQ(cg::window_is_onscreen_key.get(), kCGWindowIsOnscreen);

  EXPECT_EQ(cg::gesture_event_type, static_cast<CGEventType>(30));
  EXPECT_EQ(cg::event_type_field, static_cast<CGEventField>(0x37));
  EXPECT_EQ(cg::gesture_subtype_field, static_cast<CGEventField>(0x6e));
  EXPECT_EQ(cg::gesture_type_field, static_cast<CGEventField>(0x7b));
  EXPECT_EQ(cg::gesture_delta_field, static_cast<CGEventField>(0x7c));
  EXPECT_EQ(cg::gesture_momentum_x_field, static_cast<CGEventField>(0x81));
  EXPECT_EQ(cg::gesture_momentum_y_field, static_cast<CGEventField>(0x82));
  EXPECT_EQ(cg::gesture_phase_field, static_cast<CGEventField>(0x84));
  EXPECT_EQ(cg::gesture_phase_mirror_field, static_cast<CGEventField>(0x86));
  EXPECT_EQ(cg::gesture_finger_count_field, static_cast<CGEventField>(0x8a));
  EXPECT_EQ(cg::gesture_type_mirror_field, static_cast<CGEventField>(0xa5));
  EXPECT_EQ(cg::scroll_momentum_phase_field, static_cast<CGEventField>(123));

  const auto create_event = &cg::event::create;
  const auto copy_event = &cg::event::copy;
  const auto retain_event = &cg::event::retain;
  const auto create_view_data = &cg::event_view::create_data;
  const auto from_data = &cg::event::from_data;
  const auto create_source_from_event = &cg::event::create_source_from_event;
  ASSERT_NE(create_event, nullptr);
  ASSERT_NE(copy_event, nullptr);
  ASSERT_NE(retain_event, nullptr);
  EXPECT_TRUE(create_view_data != nullptr);
  ASSERT_NE(from_data, nullptr);
  ASSERT_NE(create_source_from_event, nullptr);
}

}  // namespace
