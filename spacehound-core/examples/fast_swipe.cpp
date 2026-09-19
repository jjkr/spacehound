// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#include <ApplicationServices/ApplicationServices.h>

#include <cstdlib>
#include <iostream>
#include <string_view>

#include <spacehound/ax.hpp>
#include <spacehound/cg.hpp>
#include <spacehound/gesture.hpp>

namespace {

namespace cg = spacehound::cg;
namespace gesture = spacehound::gesture;

void print_usage(const char *program_name) {
  std::cerr << "Usage: " << program_name << " left|right|up|down\n";
}

auto parse_direction(
    std::string_view argument,
    gesture::direction &out_direction) -> bool {
  if (argument == "left") {
    out_direction = gesture::direction::left;
    return true;
  }

  if (argument == "right") {
    out_direction = gesture::direction::right;
    return true;
  }

  if (argument == "up") {
    out_direction = gesture::direction::up;
    return true;
  }

  if (argument == "down") {
    out_direction = gesture::direction::down;
    return true;
  }

  return false;
}

auto direction_label(gesture::direction value) -> std::string_view {
  switch (value) {
    case gesture::direction::left:
      return "left";
    case gesture::direction::right:
      return "right";
    case gesture::direction::up:
      return "up";
    case gesture::direction::down:
      return "down";
  }

  return "unknown";
}

auto post_swipe(gesture::direction swipe_direction) -> bool {
  const auto source = cg::event_source::create(kCGEventSourceStateHIDSystemState);
  if (!source) {
    std::cerr << "Failed to create CoreGraphics event source.\n";
    return false;
  }

  return gesture::post_swipe(source.view(), swipe_direction);
}

}  // namespace

int main(int argc, char **argv) {
  gesture::direction swipe_direction{};

  if (argc != 2) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (!parse_direction(argv[1], swipe_direction)) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (!spacehound::ax::is_process_trusted()) {
    std::cerr
        << "Accessibility permission is required. Enable it in System Settings "
        << "> Privacy & Security > Accessibility and try again.\n";
    return EXIT_FAILURE;
  }

  if (!post_swipe(swipe_direction)) {
    std::cerr << "Failed to create or post synthetic swipe gesture events.\n";
    return EXIT_FAILURE;
  }

  std::cout
      << "Posted synthetic Fast Swipe gesture (" << direction_label(swipe_direction)
      << "). macOS may still ignore it depending on system settings.\n";
  return EXIT_SUCCESS;
}
