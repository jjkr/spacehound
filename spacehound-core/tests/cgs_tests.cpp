// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <ApplicationServices/ApplicationServices.h>

#include <spacehound/cf.hpp>
#include <spacehound/cgs.hpp>

namespace {

namespace cf = spacehound::cf;
namespace cgs = spacehound::cgs;

TEST(cgs_tests, private_space_queries_are_callable) {
  const auto connection = cgs::main_connection_id();

  const auto managed_spaces = cgs::copy_managed_display_spaces(connection);
  if (managed_spaces) {
    EXPECT_TRUE(cf::is<CFArrayRef>(cf::view{static_cast<CFTypeRef>(managed_spaces.get())}));
  }

  const auto active_display = cgs::copy_active_menu_bar_display_identifier(connection);
  if (active_display) {
    EXPECT_FALSE(active_display.empty());
  }

  const auto empty_array = cf::adopt(CFArrayCreate(
      kCFAllocatorDefault, nullptr, 0, &kCFTypeArrayCallBacks));
  ASSERT_TRUE(empty_array);

  const auto spaces_for_windows =
      cgs::copy_spaces_for_windows(connection, 0U, cf::view{empty_array.get()});
  if (spaces_for_windows) {
    EXPECT_TRUE(cf::is<CFArrayRef>(cf::view{static_cast<CFTypeRef>(spaces_for_windows.get())}));
  }

  EXPECT_NO_THROW((void)cgs::add_windows_to_spaces(
      connection, cf::view{empty_array.get()}, cf::view{empty_array.get()}));
  EXPECT_NO_THROW((void)cgs::remove_windows_from_spaces(
      connection, cf::view{empty_array.get()}, cf::view{empty_array.get()}));
  EXPECT_NO_THROW((void)cgs::set_cursor_in_background(false));

  const auto set_active_display = &cgs::set_active_menu_bar_display_identifier;
  ASSERT_NE(set_active_display, nullptr);
}

}  // namespace
