#include <gtest/gtest.h>

#include <unistd.h>

#include <type_traits>

#include <spacerabbit/ns.hpp>

namespace {

namespace ns = spacerabbit::ns;

static_assert(!std::is_copy_constructible_v<ns::running_application>);
static_assert(!std::is_copy_assignable_v<ns::running_application>);
static_assert(!std::is_copy_constructible_v<ns::screen>);
static_assert(!std::is_copy_assignable_v<ns::screen>);
static_assert(!std::is_copy_constructible_v<ns::workspace>);
static_assert(!std::is_copy_assignable_v<ns::workspace>);
static_assert(!std::is_copy_constructible_v<ns::notification_center>);
static_assert(!std::is_copy_assignable_v<ns::notification_center>);
static_assert(!std::is_copy_constructible_v<ns::notification_observer>);
static_assert(!std::is_copy_assignable_v<ns::notification_observer>);

void noop_callback(void *) {}

TEST(ns_tests, running_application_wrapper_is_callable) {
  const auto with_process_identifier = &ns::running_application::with_process_identifier;
  ASSERT_NE(with_process_identifier, nullptr);

  const auto application = ns::running_application::with_process_identifier(getpid());
  if (!application) {
    GTEST_SKIP() << "NSRunningApplication did not expose the current process in this environment.";
  }

  (void)application.is_active();
  (void)application.activate(ns::application_activation_options{0});
}

TEST(ns_tests, screen_workspace_and_notifications_are_exposed) {
  const auto screen = ns::screen::main();
  if (screen) {
    EXPECT_GE(screen.backing_scale_factor(), 1.0);
  }

  const auto workspace = ns::workspace::shared();
  ASSERT_TRUE(workspace);

  const auto center = workspace.notification_center();
  ASSERT_TRUE(center);

  const auto notification = ns::active_space_did_change_notification();
  ASSERT_TRUE(notification);
  EXPECT_FALSE(notification.empty());

  const auto observer = center.add_observer(notification, &noop_callback, nullptr);
  EXPECT_TRUE(observer);
}

TEST(ns_tests, activation_option_constants_match_appkit_values) {
  EXPECT_EQ(ns::activate_all_windows, ns::application_activation_options{1} << 0);
  EXPECT_EQ(
      ns::activate_ignoring_other_apps,
      ns::application_activation_options{1} << 1);
}

}  // namespace
