#include <gtest/gtest.h>

#include <ApplicationServices/ApplicationServices.h>
#include <unistd.h>

#include <type_traits>

#include <spacehound/ax.hpp>
#include <spacehound/cf.hpp>

namespace {

namespace ax = spacehound::ax;
namespace cf = spacehound::cf;

static_assert(!std::is_copy_constructible_v<ax::value>);
static_assert(!std::is_copy_assignable_v<ax::value>);
static_assert(!std::is_copy_constructible_v<ax::ui_element>);
static_assert(!std::is_copy_assignable_v<ax::ui_element>);
static_assert(!std::is_copy_constructible_v<ax::observer>);
static_assert(!std::is_copy_assignable_v<ax::observer>);
static_assert(std::is_trivially_copyable_v<ax::value_view>);
static_assert(std::is_trivially_copyable_v<ax::ui_element_view>);
static_assert(std::is_trivially_copyable_v<ax::observer_view>);
static_assert(!std::is_convertible_v<ax::value, AXValueRef>);
static_assert(!std::is_convertible_v<ax::ui_element, AXUIElementRef>);
static_assert(!std::is_convertible_v<ax::observer, AXObserverRef>);

void observer_callback(
    AXObserverRef,
    AXUIElementRef,
    CFStringRef,
    void *) {}

TEST(ax_tests, ui_element_view_defaults_to_null) {
  const ax::ui_element_view view{};
  EXPECT_FALSE(view);
  EXPECT_EQ(view.get(), nullptr);
}

TEST(ax_tests, value_view_defaults_to_null) {
  const ax::value_view view{};
  EXPECT_FALSE(view);
  EXPECT_EQ(view.get(), nullptr);
  EXPECT_EQ(view.type(), kAXValueIllegalType);
}

TEST(ax_tests, observer_view_defaults_to_null) {
  const ax::observer_view view{};
  EXPECT_FALSE(view);
  EXPECT_EQ(view.get(), nullptr);
}

TEST(ax_tests, ui_element_create_application_retain_and_move_preserve_underlying_ref) {
  auto app = ax::ui_element::create_application(getpid());
  ASSERT_TRUE(app);

  const auto retained = ax::ui_element::retain(app.view());
  ASSERT_TRUE(retained);
  EXPECT_EQ(retained.get(), app.get());

  auto moved = std::move(app);
  EXPECT_FALSE(app);
  ASSERT_TRUE(moved);
  EXPECT_NE(moved.get(), nullptr);
}

TEST(ax_tests, value_create_and_get_value_round_trip_geometry) {
  const CGPoint point = CGPointMake(10.5, 22.25);
  const CGSize size = CGSizeMake(30.0, 44.0);
  const CGRect rect = CGRectMake(1.0, 2.0, 3.0, 4.0);

  const auto point_value = ax::value::create(point);
  const auto size_value = ax::value::create(size);
  auto rect_value = ax::value::create(rect);

  ASSERT_TRUE(point_value);
  ASSERT_TRUE(size_value);
  ASSERT_TRUE(rect_value);
  EXPECT_EQ(point_value.type(), kAXValueCGPointType);
  EXPECT_EQ(size_value.type(), kAXValueCGSizeType);
  EXPECT_EQ(rect_value.type(), kAXValueCGRectType);

  CGPoint decoded_point{};
  CGSize decoded_size{};
  CGRect decoded_rect{};
  EXPECT_TRUE(point_value.get_value(decoded_point));
  EXPECT_TRUE(size_value.get_value(decoded_size));
  EXPECT_TRUE(rect_value.get_value(decoded_rect));
  EXPECT_DOUBLE_EQ(decoded_point.x, point.x);
  EXPECT_DOUBLE_EQ(decoded_point.y, point.y);
  EXPECT_DOUBLE_EQ(decoded_size.width, size.width);
  EXPECT_DOUBLE_EQ(decoded_size.height, size.height);
  EXPECT_DOUBLE_EQ(decoded_rect.origin.x, rect.origin.x);
  EXPECT_DOUBLE_EQ(decoded_rect.origin.y, rect.origin.y);
  EXPECT_DOUBLE_EQ(decoded_rect.size.width, rect.size.width);
  EXPECT_DOUBLE_EQ(decoded_rect.size.height, rect.size.height);

  const auto retained = ax::value::retain(rect_value.view());
  ASSERT_TRUE(retained);
  EXPECT_EQ(retained.type(), kAXValueCGRectType);

  auto moved = std::move(rect_value);
  EXPECT_FALSE(rect_value);
  ASSERT_TRUE(moved);
  EXPECT_EQ(moved.type(), kAXValueCGRectType);
}

TEST(ax_tests, observer_create_and_run_loop_source_succeed_for_current_process) {
  AXError error = kAXErrorSuccess;
  const auto observer = ax::observer::create(getpid(), &observer_callback, &error);
  ASSERT_EQ(error, kAXErrorSuccess);
  ASSERT_TRUE(observer);
  EXPECT_TRUE(observer.run_loop_source());

  const auto retained = ax::observer::retain(observer.view());
  ASSERT_TRUE(retained);
  EXPECT_TRUE(retained.run_loop_source());
}

TEST(ax_tests, trust_helpers_and_constants_are_callable) {
  const cf::dictionary_entry entries[] = {{
      .key = ax::trusted_check_option_prompt,
      .value = cf::type_view{kCFBooleanFalse},
  }};
  const auto options = cf::dictionary::create(entries);
  ASSERT_TRUE(options);

  EXPECT_EQ(ax::trusted_check_option_prompt.get(), kAXTrustedCheckOptionPrompt);
  EXPECT_EQ(ax::windows_attribute.get(), kAXWindowsAttribute);
  EXPECT_EQ(ax::focused_window_attribute.get(), kAXFocusedWindowAttribute);
  EXPECT_EQ(ax::main_attribute.get(), kAXMainAttribute);
  EXPECT_EQ(ax::title_attribute.get(), kAXTitleAttribute);
  EXPECT_EQ(ax::position_attribute.get(), kAXPositionAttribute);
  EXPECT_EQ(ax::size_attribute.get(), kAXSizeAttribute);
  EXPECT_TRUE(ax::frame_attribute.equals(cf::string_view{CFSTR("AXFrame")}));
  EXPECT_EQ(ax::children_attribute.get(), kAXChildrenAttribute);
  EXPECT_EQ(ax::selected_children_attribute.get(), kAXSelectedChildrenAttribute);
  EXPECT_EQ(ax::identifier_attribute.get(), kAXIdentifierAttribute);
  EXPECT_EQ(ax::subrole_attribute.get(), kAXSubroleAttribute);
  EXPECT_EQ(ax::description_attribute.get(), kAXDescriptionAttribute);
  EXPECT_EQ(ax::selected_attribute.get(), kAXSelectedAttribute);
  EXPECT_EQ(ax::focused_attribute.get(), kAXFocusedAttribute);
  EXPECT_EQ(ax::raise_action.get(), kAXRaiseAction);

  const auto trusted = ax::is_process_trusted();
  const auto trusted_with_options = ax::is_process_trusted_with_options(options.view());
  (void)trusted;
  (void)trusted_with_options;
}

TEST(ax_tests, ui_element_copy_attribute_value_reports_errors_without_leaking_output) {
  const auto app = ax::ui_element::create_application(getpid());
  ASSERT_TRUE(app);

  cf::type attribute_value = cf::type::retain(cf::type_view{kCFBooleanTrue});
  ASSERT_TRUE(attribute_value);

  const auto error =
      app.copy_attribute_value(cf::string_view{CFSTR("AXDefinitelyMissing")}, attribute_value);
  EXPECT_NE(error, kAXErrorSuccess);
  EXPECT_FALSE(attribute_value);
}

TEST(ax_tests, ui_element_name_and_settability_queries_are_callable) {
  const auto app = ax::ui_element::create_application(getpid());
  ASSERT_TRUE(app);

  cf::type names = cf::type::retain(cf::type_view{kCFBooleanTrue});
  if (app.view().copy_attribute_names(names) == kAXErrorSuccess) {
    EXPECT_TRUE(names && names.is<CFArrayRef>());
  } else {
    EXPECT_FALSE(names);
  }

  names = cf::type::retain(cf::type_view{kCFBooleanTrue});
  if (app.view().copy_action_names(names) == kAXErrorSuccess) {
    EXPECT_TRUE(names && names.is<CFArrayRef>());
  } else {
    EXPECT_FALSE(names);
  }

  bool settable = true;
  const auto error = app.view().is_attribute_settable(
      cf::string_view{CFSTR("AXDefinitelyMissing")}, settable);
  if (error != kAXErrorSuccess) {
    EXPECT_FALSE(settable);
  }
}

}  // namespace
