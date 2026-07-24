#include <ApplicationServices/ApplicationServices.h>

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string_view>

#include <spacehound/ax.hpp>
#include <spacehound/cf.hpp>
#include <spacehound/cg.hpp>
#include <spacehound/cgs.hpp>
#include <spacehound/gesture.hpp>

namespace {

namespace ax = spacehound::ax;
namespace cf = spacehound::cf;
namespace cg = spacehound::cg;
namespace cgs = spacehound::cgs;
namespace gesture = spacehound::gesture;

struct tap_context {
  cg::event_tap_view tap{};
  cg::event_source synthetic_source{};
};

struct space_bounds {
  std::int64_t current_index{};
  std::int64_t num_spaces{};
};

inline constexpr auto synthetic_event_marker = 0x5348544150494E47LL;
inline const auto display_identifier_key = cf::string_view{CFSTR("Display Identifier")};
inline const auto spaces_key = cf::string_view{CFSTR("Spaces")};
inline const auto current_space_key = cf::string_view{CFSTR("Current Space")};
inline const auto managed_space_id_key = cf::string_view{CFSTR("ManagedSpaceID")};

auto direction_name(gesture::direction value) -> std::string_view {
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

auto is_horizontal_direction(gesture::direction value) -> bool {
  return value == gesture::direction::left || value == gesture::direction::right;
}

auto dictionary_number_int64(
    cf::dictionary_view dictionary,
    cf::string_view key,
    std::int64_t &out_value) -> bool {
  const auto number = dictionary.find<CFNumberRef>(key);
  if (!number) {
    return false;
  }

  const auto value = cf::number_get<std::int64_t>(number);
  if (!value) {
    return false;
  }

  out_value = *value;
  return true;
}

auto active_display_space_bounds(space_bounds &out_bounds) -> bool {
  const auto connection = cgs::main_connection_id();
  const auto active_display = cgs::copy_active_menu_bar_display_identifier(connection);
  if (!active_display) {
    return false;
  }

  const auto managed_spaces = cgs::copy_managed_display_spaces(connection);
  if (!managed_spaces) {
    return false;
  }

  const auto managed_spaces_view = cf::view<CFArrayRef>{managed_spaces.get()};
  const auto display_count = CFArrayGetCount(managed_spaces_view.get());
  for (CFIndex display_index = 0; display_index < display_count; ++display_index) {
    const auto display_dict_ref =
        cf::array_at<CFDictionaryRef>(managed_spaces_view, display_index);
    if (!display_dict_ref) {
      continue;
    }

    const auto display_dict = cf::dictionary_view{display_dict_ref};
    const auto display_identifier =
        display_dict.find<CFStringRef>(display_identifier_key);
    if (!display_identifier || !active_display.equals(cf::string_view{display_identifier})) {
      continue;
    }

    const auto spaces = display_dict.find<CFArrayRef>(spaces_key);
    const auto current_space_ref =
        display_dict.find<CFDictionaryRef>(current_space_key);
    if (!spaces || !current_space_ref) {
      break;
    }

    std::int64_t current_space_id = 0;
    if (!dictionary_number_int64(
            cf::dictionary_view{current_space_ref},
            managed_space_id_key,
            current_space_id)) {
      break;
    }

    const auto space_count = CFArrayGetCount(spaces.get());
    for (CFIndex space_index = 0; space_index < space_count; ++space_index) {
      const auto space_dict_ref = cf::array_at<CFDictionaryRef>(spaces, space_index);
      if (!space_dict_ref) {
        continue;
      }

      std::int64_t space_id = 0;
      if (!dictionary_number_int64(
              cf::dictionary_view{space_dict_ref},
              managed_space_id_key,
              space_id)) {
        continue;
      }

      if (space_id == current_space_id) {
        out_bounds.current_index = static_cast<std::int64_t>(space_index);
        out_bounds.num_spaces = static_cast<std::int64_t>(space_count);
        return true;
      }
    }

    break;
  }

  return false;
}

auto decode_direction(
    cg::event_view event,
    gesture::direction &out_direction) -> bool {
  const auto event_gesture_type = event.integer_field(cg::gesture_type_field);
  const auto delta = event.double_field(cg::gesture_delta_field);

  switch (event_gesture_type) {
    case 1:
      out_direction =
          (delta > 0.0) ? gesture::direction::right : gesture::direction::left;
      return true;
    case 2:
      out_direction =
          (delta > 0.0) ? gesture::direction::up : gesture::direction::down;
      return true;
    default:
      return false;
  }
}

auto decode_phase(cg::event_view event, gesture::phase &out_phase) -> bool {
  switch (event.integer_field(cg::gesture_phase_field)) {
    case static_cast<std::int64_t>(gesture::phase::begin):
      out_phase = gesture::phase::begin;
      return true;
    case static_cast<std::int64_t>(gesture::phase::update):
      out_phase = gesture::phase::update;
      return true;
    case static_cast<std::int64_t>(gesture::phase::end):
      out_phase = gesture::phase::end;
      return true;
    default:
      return false;
  }
}

auto event_tap_callback(
    CGEventTapProxy proxy,
    CGEventType type,
    CGEventRef event_ref,
    void *user_info) -> CGEventRef {
  auto *context = static_cast<tap_context *>(user_info);
  const auto event = cg::event_view{event_ref};

  if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
    const char *reason =
        (type == kCGEventTapDisabledByTimeout) ? "timeout" : "user input";

    if (context != nullptr && context->tap) {
      std::cerr << "Event tap disabled by " << reason << ", re-enabling.\n";
      context->tap.enable(true);
    }
    return event_ref;
  }

  if (type != cg::gesture_event_type) {
    return event_ref;
  }

  if (event.integer_field(kCGEventSourceUserData) == synthetic_event_marker) {
    return event_ref;
  }

  gesture::phase phase{};
  gesture::direction swipe_direction{};
  if (!decode_phase(event, phase) || !decode_direction(event, swipe_direction)) {
    return event_ref;
  }

  if (phase == gesture::phase::begin) {
    if (is_horizontal_direction(swipe_direction)) {
      space_bounds bounds{};

      if (active_display_space_bounds(bounds)) {
        if (swipe_direction == gesture::direction::left &&
            bounds.current_index == 0) {
          std::cout << "Ignoring left begin gesture at first space.\n";
          std::cout.flush();
          return nullptr;
        }

        if (swipe_direction == gesture::direction::right &&
            bounds.current_index >= bounds.num_spaces - 1) {
          std::cout << "Ignoring right begin gesture at last space.\n";
          std::cout.flush();
          return nullptr;
        }
      } else {
        std::cerr
            << "Failed to determine active display space bounds; replaying gesture.\n";
      }
    }

    std::cout << "Intercepted begin gesture: " << direction_name(swipe_direction) << '\n';
    std::cout.flush();

    // Swallow the original begin event and replace it with the fast synthetic sequence.
    if (!context ||
        !gesture::post_swipe(proxy, context->synthetic_source.view(), swipe_direction)) {
      std::cerr << "Failed to replay synthetic fast swipe for "
                << direction_name(swipe_direction) << ".\n";
      return event_ref;
    }
  }

  return nullptr;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 1) {
    std::cerr << "Usage: " << argv[0] << '\n';
    return EXIT_FAILURE;
  }

  if (!ax::is_process_trusted()) {
    std::cerr
        << "Accessibility permission is required. Enable it in System Settings "
        << "> Privacy & Security > Accessibility and try again.\n";
    return EXIT_FAILURE;
  }

  tap_context context{};
  context.synthetic_source =
      cg::event_source::create(kCGEventSourceStateHIDSystemState);
  if (!context.synthetic_source) {
    std::cerr << "Failed to create synthetic CoreGraphics event source.\n";
    return EXIT_FAILURE;
  }

  context.synthetic_source.set_user_data(synthetic_event_marker);

  const auto event_mask = CGEventMaskBit(cg::gesture_event_type);
  auto tap = cg::event_tap::create(
      kCGHIDEventTap,
      kCGHeadInsertEventTap,
      kCGEventTapOptionDefault,
      event_mask,
      event_tap_callback,
      &context);
  if (!tap) {
    std::cerr << "Failed to create HID event tap.\n";
    return EXIT_FAILURE;
  }

  context.tap = tap.view();

  const auto run_loop_source = tap.create_run_loop_source();
  if (!run_loop_source) {
    std::cerr << "Failed to create run loop source for event tap.\n";
    return EXIT_FAILURE;
  }

  cf::add_source(
      cf::current_run_loop(),
      cf::view<CFRunLoopSourceRef>{run_loop_source.get()},
      cf::string_view{kCFRunLoopCommonModes});
  tap.enable(true);

  std::cout << "fast_swipe_tap listening for begin gesture events.\n";
  std::cout.flush();

  cf::run();
  return EXIT_SUCCESS;
}
