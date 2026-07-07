#include <gtest/gtest.h>

#include <unistd.h>

#include <spacerabbit/cg.hpp>

namespace {

TEST(live_system_tests, post_to_current_process_does_not_crash_for_event_and_view) {
  // Posting back to the current process avoids requiring a second target app.
  const auto source =
      spacerabbit::cg::event_source::create(kCGEventSourceStateHIDSystemState);
  ASSERT_TRUE(source);

  const auto event = spacerabbit::cg::event::create(source.view());
  ASSERT_TRUE(event);

  event.set_type(kCGEventNull);

  EXPECT_NO_THROW(event.post_to_pid(getpid()));
  EXPECT_NO_THROW(event.view().post_to_pid(getpid()));
}

}  // namespace
