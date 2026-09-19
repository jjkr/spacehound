#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "internal/control_internal.hpp"

namespace {

namespace control = spacehound::control;
namespace detail = spacehound::control::detail;
namespace gesture = spacehound::gesture;

auto make_rect(double x, double y, double width, double height) -> CGRect {
  return CGRectMake(x, y, width, height);
}

TEST(control_tests, action_names_are_stable_and_omit_request_indices) {
  EXPECT_EQ(
      detail::action_name(control::workspace_request{
          .action = control::workspace_action::left,
      }),
      "workspace-left");
  EXPECT_EQ(
      detail::action_name(control::workspace_request{
          .action = control::workspace_action::right,
      }),
      "workspace-right");
  EXPECT_EQ(
      detail::action_name(control::workspace_request{
          .action = control::workspace_action::go_to,
          .index = 42,
      }),
      "workspace-go-to");

  EXPECT_EQ(
      detail::action_name(control::display_request{
          .action = control::display_action::left,
      }),
      "display-left");
  EXPECT_EQ(
      detail::action_name(control::display_request{
          .action = control::display_action::right,
      }),
      "display-right");
  EXPECT_EQ(
      detail::action_name(control::display_request{
          .action = control::display_action::go_to,
          .index = 42,
      }),
      "display-go-to");

  EXPECT_EQ(
      detail::action_name(control::window_focus_request{
          .direction = control::window_focus_direction::next,
      }),
      "window-focus-next");
  EXPECT_EQ(
      detail::action_name(control::window_focus_request{
          .direction = control::window_focus_direction::previous,
      }),
      "window-focus-previous");

  EXPECT_EQ(
      detail::action_name(control::system_ui_request{
          .element = control::system_ui_element::mission_control,
      }),
      "system-ui-mission-control");
  EXPECT_EQ(
      detail::action_name(control::system_ui_request{
          .element = control::system_ui_element::expose,
      }),
      "system-ui-expose");
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

TEST(control_tests, main_display_identifier_represents_unified_spaces) {
  EXPECT_TRUE(detail::is_unified_spaces_display_identifier("Main"));
  EXPECT_FALSE(detail::is_unified_spaces_display_identifier("main"));
  EXPECT_FALSE(detail::is_unified_spaces_display_identifier(
      "37D8832A-2D66-02CA-B9F7-8F30A301B230"));
}

TEST(control_tests, display_index_containing_point_uses_half_open_bounds) {
  const std::vector<detail::display_record> displays{
      detail::display_record{.display_id = 1, .uuid = "left", .bounds = make_rect(0, 0, 1000, 800)},
      detail::display_record{.display_id = 2, .uuid = "right", .bounds = make_rect(1000, -100, 1200, 900)},
  };

  EXPECT_EQ(detail::find_display_index_containing_point(displays, CGPointMake(10, 10)), 0U);
  EXPECT_EQ(detail::find_display_index_containing_point(displays, CGPointMake(1500, -50)), 1U);
  // The shared edge belongs to exactly one display.
  EXPECT_EQ(detail::find_display_index_containing_point(displays, CGPointMake(1000, 10)), 1U);
  EXPECT_EQ(detail::find_display_index_containing_point(displays, CGPointMake(999.5, 10)), 0U);
  EXPECT_EQ(detail::find_display_index_containing_point(displays, CGPointMake(-1, 10)), std::nullopt);
  EXPECT_EQ(detail::find_display_index_containing_point(displays, CGPointMake(500, 900)), std::nullopt);
  EXPECT_EQ(detail::find_display_index_containing_point({}, CGPointMake(0, 0)), std::nullopt);
}

TEST(control_tests, menu_bar_click_candidates_start_in_the_middle_and_fan_out) {
  const auto target = make_rect(-3360.0, -351.0, 3360.0, 1890.0);
  const auto candidates = detail::menu_bar_click_candidates(target);

  ASSERT_EQ(candidates.size(), 11U);
  EXPECT_DOUBLE_EQ(candidates[0].x, -3360.0 + 1680.0);
  EXPECT_DOUBLE_EQ(candidates[1].x, -3360.0 + 1512.0);
  EXPECT_DOUBLE_EQ(candidates[2].x, -3360.0 + 1848.0);
  EXPECT_DOUBLE_EQ(candidates.back().x, -3360.0 + 2520.0);
  for (const auto &candidate : candidates) {
    EXPECT_DOUBLE_EQ(candidate.y, -351.0 + 10.0);
    EXPECT_GE(candidate.x, target.origin.x);
    EXPECT_LT(candidate.x, target.origin.x + target.size.width);
  }
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

auto make_thumbnail(CGWindowID window_id, CGRect frame) -> detail::thumbnail_record {
  return detail::thumbnail_record{.window_id = window_id, .frame = frame};
}

auto thumbnail_window_ids(std::span<const detail::thumbnail_record> thumbnails)
    -> std::vector<CGWindowID> {
  std::vector<CGWindowID> ids;
  for (const auto &thumbnail : thumbnails) {
    ids.push_back(thumbnail.window_id);
  }
  return ids;
}

TEST(control_tests, dock_view_state_names_are_stable) {
  EXPECT_EQ(detail::dock_view_state_name(detail::dock_view_state::hidden), "hidden");
  EXPECT_EQ(detail::dock_view_state_name(detail::dock_view_state::mission_control), "mission-control");
  EXPECT_EQ(detail::dock_view_state_name(detail::dock_view_state::expose), "expose");
}

auto make_window(CGWindowID window_id, CGRect bounds, std::int64_t layer = 0, bool onscreen = true)
    -> detail::window_record {
  return detail::window_record{
      .window_id = window_id, .pid = 1, .layer = layer, .bounds = bounds, .is_onscreen = onscreen};
}

TEST(control_tests, thumbnails_on_display_filter_windows_and_sort_in_reading_order) {
  const CGRect display = make_rect(0, 0, 1000, 800);
  // Two rows of 200-high thumbnails; row members jitter within half a height.
  const std::vector<detail::window_record> windows{
      make_window(1U, make_rect(600, 120, 300, 200)),          // row 1, right
      make_window(2U, make_rect(1200, 0, 300, 200)),           // other display
      make_window(3U, make_rect(50, 420, 300, 200)),           // row 2, left
      make_window(4U, make_rect(20, 60, 300, 200)),            // row 1, left (jittered up)
      make_window(5U, make_rect(400, 400, 300, 200)),          // row 2, right
      make_window(6U, make_rect(0, 0, 300, 200), 25),          // not layer 0
      make_window(7U, make_rect(0, 0, 300, 200), 0, false),    // off screen
      make_window(8U, make_rect(0, 0, 0, 0)),                  // empty
      {.window_id = 9U, .pid = 1, .owner_name = "WindowManager", .layer = 0,
       .bounds = make_rect(20, 60, 300, 200), .is_onscreen = true},     // the highlight frame
      {.window_id = 10U, .pid = 1, .layer = 0, .bounds = make_rect(400, 400, 300, 200),
       .is_onscreen = true, .alpha = 0.0},                              // invisible
  };

  const auto thumbnails = detail::thumbnails_on_display(std::span{windows}, display);
  EXPECT_EQ(thumbnail_window_ids(thumbnails), (std::vector<CGWindowID>{4U, 1U, 3U, 5U}));
  EXPECT_TRUE(detail::thumbnails_on_display({}, display).empty());

  // App Exposé: only the exposed app's windows are thumbnails.
  auto one_app = windows;
  one_app[2].pid = 2;  // window 3 belongs to another app
  EXPECT_EQ(
      thumbnail_window_ids(detail::thumbnails_on_display(std::span{one_app}, display, 1)),
      (std::vector<CGWindowID>{4U, 1U, 5U}));
}

TEST(control_tests, next_display_with_thumbnails_skips_empty_displays) {
  const std::vector<detail::display_record> displays{
      {.display_id = 1, .uuid = "a", .bounds = make_rect(0, 0, 1000, 800)},
      {.display_id = 2, .uuid = "b", .bounds = make_rect(1000, 0, 1000, 800)},
      {.display_id = 3, .uuid = "c", .bounds = make_rect(2000, 0, 1000, 800)},
  };
  // Windows on displays a and c only; the one on b belongs to another app.
  const std::vector<detail::window_record> windows{
      make_window(1U, make_rect(100, 100, 300, 200)),
      make_window(2U, make_rect(2100, 100, 300, 200)),
      {.window_id = 3U, .pid = 2, .layer = 0, .bounds = make_rect(1100, 100, 300, 200), .is_onscreen = true},
  };
  const auto next = [&](std::size_t start, bool forward, bool wrap) {
    return detail::next_display_with_thumbnails(
        std::span{displays}, std::span{windows}, 1, start, forward, wrap);
  };

  EXPECT_EQ(next(1, true, false), 2U);   // b is empty: on to c
  EXPECT_EQ(next(1, false, false), 0U);  // and back to a
  EXPECT_EQ(next(2, true, false), 2U);   // c itself has windows
  EXPECT_EQ(next(1, true, true), 2U);
  EXPECT_EQ(detail::next_display_with_thumbnails(
                std::span{displays}, std::span{windows}, 3, 1, true, false),
            std::nullopt);               // nothing for pid 3 without wrapping
  EXPECT_EQ(detail::next_display_with_thumbnails(
                std::span{displays}, std::span{windows}, 3, 1, true, true),
            std::nullopt);               // nor with it
  EXPECT_EQ(next(5, true, true), std::nullopt);
}

TEST(control_tests, thumbnail_index_lookup_uses_half_open_frames) {
  const std::vector<detail::thumbnail_record> thumbnails{
      make_thumbnail(1U, make_rect(0, 0, 100, 100)),
      make_thumbnail(2U, make_rect(100, 0, 100, 100)),
  };

  EXPECT_EQ(detail::find_thumbnail_index_containing_point(std::span{thumbnails}, CGPointMake(99, 50)), 0U);
  EXPECT_EQ(detail::find_thumbnail_index_containing_point(std::span{thumbnails}, CGPointMake(100, 50)), 1U);
  EXPECT_EQ(detail::find_thumbnail_index_containing_point(std::span{thumbnails}, CGPointMake(250, 50)), std::nullopt);
}

TEST(control_tests, thumbnail_cycle_planner_starts_from_the_cursor_or_the_ends) {
  const std::vector<detail::thumbnail_record> thumbnails{
      make_thumbnail(11U, make_rect(0, 0, 100, 100)),
      make_thumbnail(12U, make_rect(100, 0, 100, 100)),
      make_thumbnail(13U, make_rect(200, 0, 100, 100)),
  };
  const control::window_focus_request next{.direction = control::window_focus_direction::next};
  const control::window_focus_request previous{
      .direction = control::window_focus_direction::previous};

  // Cursor over the middle thumbnail: step away from it.
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(next, "display-a", std::span{thumbnails}, std::nullopt, CGPointMake(150, 50), std::nullopt, 1000U, 3000U),
      (detail::thumbnail_cycle_plan{
          .target_index = 2U,
          .next_state = detail::thumbnail_cycle_state{
              .display_uuid = "display-a",
              .window_order = {11U, 12U, 13U},
              .current_index = 2U,
              .last_cycle_timestamp_ms = 1000U,
              .cursor_x = 150.0,
              .cursor_y = 50.0,
              .target_x = 250.0,
              .target_y = 50.0,
          },
      }));
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(previous, "display-a", std::span{thumbnails}, std::nullopt, CGPointMake(150, 50), std::nullopt, 1000U, 3000U)
          .target_index,
      0U);

  // Cursor elsewhere (or unknown): first for next, last for previous.
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(next, "display-a", std::span{thumbnails}, std::nullopt, CGPointMake(900, 900), std::nullopt, 1000U, 3000U)
          .target_index,
      0U);
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(previous, "display-a", std::span{thumbnails}, std::nullopt, std::nullopt, std::nullopt, 1000U, 3000U)
          .target_index,
      2U);
}

TEST(control_tests, thumbnail_cycle_planner_continues_by_window_id_and_wraps) {
  // Re-enumerated with a new thumbnail inserted in front: the session follows
  // the window it last hovered (12), not its old index.
  const std::vector<detail::thumbnail_record> thumbnails{
      make_thumbnail(10U, make_rect(0, 0, 100, 100)),
      make_thumbnail(11U, make_rect(100, 0, 100, 100)),
      make_thumbnail(12U, make_rect(200, 0, 100, 100)),
  };
  const std::optional<detail::thumbnail_cycle_state> previous_state = detail::thumbnail_cycle_state{
      .window_order = {11U, 12U},
      .current_index = 1U,
      .last_cycle_timestamp_ms = 1000U,
      .cursor_x = 150.0,
      .cursor_y = 50.0,
  };
  const control::window_focus_request next{.direction = control::window_focus_direction::next};
  const control::window_focus_request previous{
      .direction = control::window_focus_direction::previous};

  // Next from the last thumbnail wraps to the first; the cursor (unmoved,
  // within a point) is ignored as a start point.
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(next, "display-a", std::span{thumbnails}, previous_state, CGPointMake(150.5, 50), 12U, 1500U, 3000U),
      (detail::thumbnail_cycle_plan{
          .target_index = 0U,
          .next_state = detail::thumbnail_cycle_state{
              .display_uuid = "display-a",
              .window_order = {10U, 11U, 12U},
              .current_index = 0U,
              .last_cycle_timestamp_ms = 1500U,
              .cursor_x = 150.5,
              .cursor_y = 50.0,
              .target_x = 50.0,
              .target_y = 50.0,
          },
      }));
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(previous, "display-a", std::span{thumbnails}, previous_state, CGPointMake(150, 50), 12U, 1500U, 3000U)
          .target_index,
      1U);
}

TEST(control_tests, thumbnail_cycle_planner_restarts_when_the_cursor_moved) {
  const std::vector<detail::thumbnail_record> thumbnails{
      make_thumbnail(11U, make_rect(0, 0, 100, 100)),
      make_thumbnail(12U, make_rect(100, 0, 100, 100)),
      make_thumbnail(13U, make_rect(200, 0, 100, 100)),
  };
  const std::optional<detail::thumbnail_cycle_state> previous_state = detail::thumbnail_cycle_state{
      .window_order = {11U, 12U, 13U},
      .current_index = 2U,
      .last_cycle_timestamp_ms = 1000U,
      .cursor_x = 150.0,
      .cursor_y = 50.0,
  };
  const control::window_focus_request next{.direction = control::window_focus_direction::next};

  // The mouse moved onto the first thumbnail: the highlight is there now, so
  // step away from it rather than from the session's window.
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(next, "display-a", std::span{thumbnails}, previous_state, CGPointMake(50, 50), 13U, 1500U, 3000U)
          .target_index,
      1U);
  // Moved off every thumbnail: land on the frontmost window.
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(next, "display-a", std::span{thumbnails}, previous_state, CGPointMake(900, 900), 13U, 1500U, 3000U)
          .target_index,
      2U);
}

TEST(control_tests, thumbnail_cycle_planner_restarts_after_timeout_or_when_the_window_is_gone) {
  const std::vector<detail::thumbnail_record> thumbnails{
      make_thumbnail(11U, make_rect(0, 0, 100, 100)),
      make_thumbnail(12U, make_rect(100, 0, 100, 100)),
  };
  const control::window_focus_request next{.direction = control::window_focus_direction::next};

  const std::optional<detail::thumbnail_cycle_state> timed_out = detail::thumbnail_cycle_state{
      .window_order = {11U, 12U},
      .current_index = 1U,
      .last_cycle_timestamp_ms = 1000U,
      .cursor_x = 900.0,
      .cursor_y = 900.0,
  };
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(next, "display-a", std::span{thumbnails}, timed_out, CGPointMake(900, 900), std::nullopt, 4000U, 3000U)
          .target_index,
      0U);

  const std::optional<detail::thumbnail_cycle_state> stale = detail::thumbnail_cycle_state{
      .window_order = {11U, 99U},
      .current_index = 1U,
      .last_cycle_timestamp_ms = 1000U,
      .cursor_x = 150.0,
      .cursor_y = 50.0,
  };
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(next, "display-a", std::span{thumbnails}, stale, CGPointMake(150, 50), std::nullopt, 1500U, 3000U)
          .target_index,
      0U);
}

TEST(control_tests, thumbnail_cycle_planner_lands_on_the_frontmost_window_first) {
  const std::vector<detail::thumbnail_record> thumbnails{
      make_thumbnail(11U, make_rect(0, 0, 100, 100)),
      make_thumbnail(12U, make_rect(100, 0, 100, 100)),
      make_thumbnail(13U, make_rect(200, 0, 100, 100)),
  };
  const control::window_focus_request next{.direction = control::window_focus_direction::next};
  const control::window_focus_request previous{
      .direction = control::window_focus_direction::previous};

  // No cursor hit and no session: both directions land on the frontmost window.
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(next, "display-a", std::span{thumbnails}, std::nullopt, CGPointMake(900, 900), 12U, 1000U, 3000U)
          .target_index,
      1U);
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(previous, "display-a", std::span{thumbnails}, std::nullopt, std::nullopt, 12U, 1000U, 3000U)
          .target_index,
      1U);

  // A frontmost window without a thumbnail falls back to the ends.
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(next, "display-a", std::span{thumbnails}, std::nullopt, std::nullopt, 99U, 1000U, 3000U)
          .target_index,
      0U);

  // The cursor's thumbnail wins over the frontmost window.
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(next, "display-a", std::span{thumbnails}, std::nullopt, CGPointMake(50, 50), 12U, 1000U, 3000U)
          .target_index,
      1U);
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(next, "display-a", std::span{thumbnails}, std::nullopt, CGPointMake(250, 50), 12U, 1000U, 3000U)
          .target_index,
      0U);
}

TEST(control_tests, thumbnail_session_is_current_while_the_cursor_stays_put) {
  const std::optional<detail::thumbnail_cycle_state> state = detail::thumbnail_cycle_state{
      .display_uuid = "display-a",
      .last_cycle_timestamp_ms = 1000U,
      .cursor_x = 150.0,
      .cursor_y = 50.0,
  };

  EXPECT_TRUE(detail::session_is_current(state, CGPointMake(150, 50), 1500U, 3000U));
  EXPECT_TRUE(detail::session_is_current(state, CGPointMake(150.9, 49.2), 1500U, 3000U));
  EXPECT_FALSE(detail::session_is_current(state, CGPointMake(152, 50), 1500U, 3000U));
  EXPECT_FALSE(detail::session_is_current(state, CGPointMake(150, 50), 4000U, 3000U));
  EXPECT_FALSE(detail::session_is_current(state, CGPointMake(150, 50), 900U, 3000U));
  EXPECT_FALSE(detail::session_is_current(state, std::nullopt, 1500U, 3000U));
  EXPECT_FALSE(detail::session_is_current(std::nullopt, CGPointMake(150, 50), 1500U, 3000U));
}

TEST(control_tests, frontmost_thumbnail_window_follows_window_list_order) {
  const std::vector<detail::thumbnail_record> thumbnails{
      make_thumbnail(11U, make_rect(0, 0, 100, 100)),
      make_thumbnail(12U, make_rect(100, 0, 100, 100)),
  };
  const std::vector<detail::window_record> windows{
      {.window_id = 99U, .pid = 1, .layer = 0, .bounds = make_rect(0, 0, 10, 10), .is_onscreen = true},
      {.window_id = 12U, .pid = 1, .layer = 25, .bounds = make_rect(0, 0, 10, 10), .is_onscreen = true},
      {.window_id = 12U, .pid = 1, .layer = 0, .bounds = make_rect(0, 0, 10, 10), .is_onscreen = false},
      {.window_id = 12U, .pid = 1, .layer = 0, .bounds = make_rect(0, 0, 10, 10), .is_onscreen = true},
      {.window_id = 11U, .pid = 1, .layer = 0, .bounds = make_rect(0, 0, 10, 10), .is_onscreen = true},
  };

  EXPECT_EQ(detail::frontmost_thumbnail_window(std::span{windows}, std::span{thumbnails}), 12U);
  EXPECT_EQ(detail::frontmost_thumbnail_window(std::span{windows}, {}), std::nullopt);
  EXPECT_EQ(detail::frontmost_thumbnail_window({}, std::span{thumbnails}), std::nullopt);
}

TEST(control_tests, thumbnail_cycle_planner_handles_single_and_empty_lists) {
  const control::window_focus_request next{.direction = control::window_focus_direction::next};
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(next, "display-a", {}, std::nullopt, std::nullopt, std::nullopt, 1000U, 3000U),
      detail::thumbnail_cycle_plan{});

  const std::vector<detail::thumbnail_record> single{
      make_thumbnail(11U, make_rect(0, 0, 100, 100)),
  };
  EXPECT_EQ(
      detail::plan_thumbnail_cycle(next, "display-a", std::span{single}, std::nullopt, CGPointMake(50, 50), std::nullopt, 1000U, 3000U)
          .target_index,
      0U);
}

}  // namespace
