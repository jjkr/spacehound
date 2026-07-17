#include <gtest/gtest.h>

#include <optional>
#include <span>
#include <vector>

#include "internal/control_internal.hpp"

namespace {

namespace control = spacerabbit::control;
namespace detail = spacerabbit::control::detail;
namespace gesture = spacerabbit::gesture;

auto make_rect(double x, double y, double width, double height) -> CGRect {
  return CGRectMake(x, y, width, height);
}

TEST(control_tests, workspace_motion_plans_relative_moves_and_wraps) {
  const control::workspace_request left{
      .action = control::workspace_action::left,
      .wrap = false,
  };
  EXPECT_EQ(detail::plan_workspace_request(left, 0, 4), detail::workspace_motion{});
  EXPECT_EQ(
      detail::plan_workspace_request(left, 2, 4),
      (detail::workspace_motion{
          .should_execute = true,
          .direction = gesture::direction::left,
          .repeat_count = 1,
      }));

  const control::workspace_request wrapped_left{
      .action = control::workspace_action::left,
      .wrap = true,
  };
  EXPECT_EQ(
      detail::plan_workspace_request(wrapped_left, 0, 4),
      (detail::workspace_motion{
          .should_execute = true,
          .direction = gesture::direction::right,
          .repeat_count = 3,
      }));

  const control::workspace_request wrapped_right{
      .action = control::workspace_action::right,
      .wrap = true,
  };
  EXPECT_EQ(
      detail::plan_workspace_request(wrapped_right, 3, 4),
      (detail::workspace_motion{
          .should_execute = true,
          .direction = gesture::direction::left,
          .repeat_count = 3,
      }));

  const control::workspace_request numbered{
      .action = control::workspace_action::go_to,
      .index = 5,
  };
  EXPECT_EQ(
      detail::plan_workspace_request(numbered, 1, 6),
      (detail::workspace_motion{
          .should_execute = true,
          .direction = gesture::direction::right,
          .repeat_count = 3,
      }));
  EXPECT_EQ(detail::plan_workspace_request(numbered, 4, 6), detail::workspace_motion{});
  EXPECT_EQ(detail::plan_workspace_request(numbered, 1, 4), detail::workspace_motion{});
}

TEST(control_tests, display_switch_plans_relative_and_numbered_targets) {
  const control::display_request left{
      .action = control::display_action::left,
  };
  EXPECT_EQ(detail::plan_display_request(left, 0, 3), detail::display_switch_plan{});
  EXPECT_EQ(
      detail::plan_display_request(control::display_request{
                                       .action = control::display_action::left,
                                       .wrap = true,
                                   },
                                   0,
                                   3),
      (detail::display_switch_plan{
          .should_execute = true,
          .target_index = 2,
      }));
  EXPECT_EQ(
      detail::plan_display_request(left, 2, 3),
      (detail::display_switch_plan{
          .should_execute = true,
          .target_index = 1,
      }));

  const control::display_request right{
      .action = control::display_action::right,
  };
  EXPECT_EQ(detail::plan_display_request(right, 2, 3), detail::display_switch_plan{});
  EXPECT_EQ(
      detail::plan_display_request(control::display_request{
                                       .action = control::display_action::right,
                                       .wrap = true,
                                   },
                                   2,
                                   3),
      (detail::display_switch_plan{
          .should_execute = true,
          .target_index = 0,
      }));
  EXPECT_EQ(
      detail::plan_display_request(right, 1, 3),
      (detail::display_switch_plan{
          .should_execute = true,
          .target_index = 2,
      }));

  const control::display_request numbered{
      .action = control::display_action::go_to,
      .index = 3,
  };
  EXPECT_EQ(
      detail::plan_display_request(numbered, 0, 3),
      (detail::display_switch_plan{
          .should_execute = true,
          .target_index = 2,
      }));
  EXPECT_EQ(detail::plan_display_request(numbered, 2, 3), detail::display_switch_plan{});
  EXPECT_EQ(detail::plan_display_request(numbered, 0, 2), detail::display_switch_plan{});
}

TEST(control_tests, cursor_anchor_uses_the_top_center_of_the_display) {
  const auto anchor =
      detail::cursor_anchor_point(make_rect(-1920.0, -200.0, 1920.0, 1080.0));

  EXPECT_DOUBLE_EQ(anchor.x, -960.0);
  EXPECT_DOUBLE_EQ(anchor.y, -199.0);
}

TEST(control_tests, display_helpers_sort_find_current_and_pick_frontmost_window) {
  std::vector<detail::display_record> displays{
      {.display_id = 2, .uuid = "display-right", .bounds = make_rect(1920.0, 0.0, 1920.0, 1080.0)},
      {.display_id = 1, .uuid = "display-left", .bounds = make_rect(0.0, 0.0, 1920.0, 1080.0)},
      {.display_id = 3, .uuid = "display-far-right", .bounds = make_rect(3840.0, 0.0, 1920.0, 1080.0)},
  };

  detail::sort_displays_left_to_right(displays);
  ASSERT_EQ(displays.size(), 3U);
  EXPECT_EQ(displays[0].uuid, "display-left");
  EXPECT_EQ(displays[1].uuid, "display-right");
  EXPECT_EQ(displays[2].uuid, "display-far-right");

  EXPECT_EQ(
      detail::find_current_display_index(displays, "display-right"),
      std::optional<std::size_t>{1U});
  EXPECT_FALSE(detail::find_current_display_index(displays, "missing").has_value());

  const std::vector<detail::window_record> windows{
      {
          .window_id = 11,
          .pid = 1,
          .owner_name = "FrontApp",
          .title = "Front",
          .layer = 0,
          .bounds = make_rect(2000.0, 100.0, 800.0, 600.0),
          .is_onscreen = true,
      },
      {
          .window_id = 12,
          .pid = 2,
          .owner_name = "BackApp",
          .title = "Back",
          .layer = 0,
          .bounds = make_rect(2100.0, 120.0, 800.0, 600.0),
          .is_onscreen = true,
      },
      {
          .window_id = 13,
          .pid = 3,
          .owner_name = "Straddle",
          .title = "Straddle",
          .layer = 0,
          .bounds = make_rect(1800.0, 100.0, 400.0, 600.0),
          .is_onscreen = true,
      },
      {
          .window_id = 14,
          .pid = 4,
          .owner_name = "Hidden",
          .title = "Hidden",
          .layer = 1,
          .bounds = make_rect(2200.0, 140.0, 800.0, 600.0),
          .is_onscreen = true,
      },
  };

  const auto target_bounds = make_rect(1920.0, 0.0, 1920.0, 1080.0);
  const auto target_index =
      detail::find_frontmost_window_index_on_display(std::span{windows}, target_bounds);
  ASSERT_TRUE(target_index.has_value());
  EXPECT_EQ(windows[*target_index].window_id, 11U);

  const auto empty_bounds = make_rect(5760.0, 0.0, 1920.0, 1080.0);
  EXPECT_FALSE(
      detail::find_frontmost_window_index_on_display(std::span{windows}, empty_bounds).has_value());
}

TEST(control_tests, window_cycle_candidates_only_include_visible_windows_on_target_display) {
  const std::vector<detail::window_record> windows{
      {
          .window_id = 11,
          .pid = 1,
          .owner_name = "FrontApp",
          .title = "Front",
          .layer = 0,
          .bounds = make_rect(2000.0, 100.0, 800.0, 600.0),
          .is_onscreen = true,
      },
      {
          .window_id = 12,
          .pid = 2,
          .owner_name = "BackApp",
          .title = "Back",
          .layer = 0,
          .bounds = make_rect(2100.0, 120.0, 800.0, 600.0),
          .is_onscreen = true,
      },
      {
          .window_id = 13,
          .pid = 3,
          .owner_name = "Straddle",
          .title = "Straddle",
          .layer = 0,
          .bounds = make_rect(1800.0, 100.0, 400.0, 600.0),
          .is_onscreen = true,
      },
      {
          .window_id = 14,
          .pid = 4,
          .owner_name = "HiddenLayer",
          .title = "HiddenLayer",
          .layer = 1,
          .bounds = make_rect(2200.0, 140.0, 800.0, 600.0),
          .is_onscreen = true,
      },
      {
          .window_id = 15,
          .pid = 5,
          .owner_name = "Offscreen",
          .title = "Offscreen",
          .layer = 0,
          .bounds = make_rect(2300.0, 160.0, 800.0, 600.0),
          .is_onscreen = false,
      },
      {
          .window_id = 16,
          .pid = 6,
          .owner_name = "ZeroWidth",
          .title = "ZeroWidth",
          .layer = 0,
          .bounds = make_rect(2400.0, 180.0, 0.0, 600.0),
          .is_onscreen = true,
      },
  };

  const auto target_bounds = make_rect(1920.0, 0.0, 1920.0, 1080.0);
  EXPECT_EQ(
      detail::collect_window_ids_on_display(std::span{windows}, target_bounds),
      std::vector<CGWindowID>({11U, 12U, 13U}));
}

TEST(control_tests, window_cycle_planner_starts_new_sessions_for_next_and_previous) {
  const std::vector<CGWindowID> window_ids{11U, 12U, 13U};

  EXPECT_EQ(
      detail::plan_window_focus_request(
          control::window_focus_request{
              .direction = control::window_focus_direction::next,
          },
          std::span{window_ids},
          std::nullopt,
          1000U,
          3000U),
      (detail::window_focus_plan{
          .target_window_id = 12U,
          .next_state = detail::window_cycle_state{
              .window_order = {11U, 12U, 13U},
              .current_index = 1U,
              .last_cycle_timestamp_ms = 1000U,
          },
      }));

  EXPECT_EQ(
      detail::plan_window_focus_request(
          control::window_focus_request{
              .direction = control::window_focus_direction::previous,
          },
          std::span{window_ids},
          std::nullopt,
          1000U,
          3000U),
      (detail::window_focus_plan{
          .target_window_id = 13U,
          .next_state = detail::window_cycle_state{
              .window_order = {11U, 12U, 13U},
              .current_index = 2U,
              .last_cycle_timestamp_ms = 1000U,
          },
      }));
}

TEST(control_tests, window_cycle_planner_continues_active_session_and_appends_new_windows) {
  const std::vector<CGWindowID> window_ids{11U, 12U, 14U};
  const std::optional<detail::window_cycle_state> previous_state = detail::window_cycle_state{
      .window_order = {11U, 12U},
      .current_index = 1U,
      .last_cycle_timestamp_ms = 1000U,
  };

  EXPECT_EQ(
      detail::plan_window_focus_request(
          control::window_focus_request{
              .direction = control::window_focus_direction::next,
          },
          std::span{window_ids},
          previous_state,
          1500U,
          3000U),
      (detail::window_focus_plan{
          .target_window_id = 14U,
          .next_state = detail::window_cycle_state{
              .window_order = {11U, 12U, 14U},
              .current_index = 2U,
              .last_cycle_timestamp_ms = 1500U,
          },
      }));
}

TEST(control_tests, window_cycle_planner_resets_after_timeout_or_missing_windows) {
  const std::vector<CGWindowID> timed_out_ids{11U, 12U, 13U};
  const std::optional<detail::window_cycle_state> timed_out_state = detail::window_cycle_state{
      .window_order = {11U, 12U, 13U},
      .current_index = 1U,
      .last_cycle_timestamp_ms = 1000U,
  };

  EXPECT_EQ(
      detail::plan_window_focus_request(
          control::window_focus_request{
              .direction = control::window_focus_direction::next,
          },
          std::span{timed_out_ids},
          timed_out_state,
          4500U,
          3000U),
      (detail::window_focus_plan{
          .target_window_id = 12U,
          .next_state = detail::window_cycle_state{
              .window_order = {11U, 12U, 13U},
              .current_index = 1U,
              .last_cycle_timestamp_ms = 4500U,
          },
      }));

  const std::vector<CGWindowID> missing_window_ids{11U, 13U};
  EXPECT_EQ(
      detail::plan_window_focus_request(
          control::window_focus_request{
              .direction = control::window_focus_direction::next,
          },
          std::span{missing_window_ids},
          timed_out_state,
          1500U,
          3000U),
      (detail::window_focus_plan{
          .target_window_id = 13U,
          .next_state = detail::window_cycle_state{
              .window_order = {11U, 13U},
              .current_index = 1U,
              .last_cycle_timestamp_ms = 1500U,
          },
      }));
}

TEST(control_tests, window_cycle_planner_clears_state_when_too_few_windows_exist) {
  const std::vector<CGWindowID> window_ids{11U};
  const std::optional<detail::window_cycle_state> previous_state = detail::window_cycle_state{
      .window_order = {11U, 12U},
      .current_index = 1U,
      .last_cycle_timestamp_ms = 1000U,
  };

  EXPECT_EQ(
      detail::plan_window_focus_request(
          control::window_focus_request{
              .direction = control::window_focus_direction::next,
          },
          std::span{window_ids},
          previous_state,
          1500U,
          3000U),
      detail::window_focus_plan{});
}

}  // namespace
