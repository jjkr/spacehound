// Probe for the Mission Control / App Exposé hover work: inspects the
// WindowManager accessibility tree and exercises the thumbnail enumeration and
// hover path from the core. Run from a terminal trusted for Accessibility.
//
// Subcommands wait MC_PROBE_DELAY seconds (default 3) first so Mission
// Control can be opened, e.g. `open -b com.apple.exposelauncher; mc_probe list`.
//
//   dump [max-depth] [--wm]      Dump the Dock's (or WindowManager's) AX tree.
//   list                         Visible thumbnails per display, reading order.
//   hit X Y [X Y ...]            System-wide AX hit test with ancestor chain.
//   hover-current DISPLAY INDEX [pid|hop|hide|stay]
//                                Hover a visible thumbnail; holds MC_PROBE_HOLD s.
//   expose DOCK_ITEM_TITLE       Trigger App Exposé for a Dock item and dump.
//   keys COUNT [INTERVAL_MS] [--shift]
//                                Post ⌥Tab / ⌥⇧Tab through the HID tap.
//   displays | spaces | windows  Display bounds, managed spaces, Dock windows.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <spacehound/ax.hpp>
#include <spacehound/cf.hpp>
#include <spacehound/cg.hpp>
#include <spacehound/cgs.hpp>

#include "internal/control_internal.hpp"

namespace ax = spacehound::ax;
namespace cf = spacehound::cf;
namespace cg = spacehound::cg;
namespace cgs = spacehound::cgs;
namespace detail = spacehound::control::detail;

namespace {

auto env_seconds(const char *name, double fallback) -> std::chrono::milliseconds {
  const char *value = std::getenv(name);
  return std::chrono::milliseconds{
      static_cast<long long>((value ? std::atof(value) : fallback) * 1000.0)};
}

void wait_for_start() {
  const auto delay = env_seconds("MC_PROBE_DELAY", 3.0);
  std::cout << "Waiting " << delay.count() << "ms...\n" << std::flush;
  std::this_thread::sleep_for(delay);
}

void hold() {
  std::this_thread::sleep_for(env_seconds("MC_PROBE_HOLD", 5.0));
}

auto string_attribute(ax::ui_element_view element, cf::string_view attribute) -> std::string {
  cf::type value;
  if (element.copy_attribute_value(attribute, value) != kAXErrorSuccess || !value) {
    return {};
  }

  const auto string = value.cast<CFStringRef>();
  return string ? cf::string_view{string}.to_utf8().value_or("") : std::string{};
}

auto frame_attribute(ax::ui_element_view element) -> std::optional<CGRect> {
  cf::type value;
  if (element.copy_attribute_value(ax::frame_attribute, value) != kAXErrorSuccess || !value) {
    return std::nullopt;
  }

  CGRect frame{};
  if (!ax::value_view{value.cast<AXValueRef>().get()}.get_value(frame)) {
    return std::nullopt;
  }

  return frame;
}

auto number_int64(cf::dictionary_view dictionary, cf::string_view key) -> std::int64_t {
  const auto number = dictionary.find<CFNumberRef>(key);
  std::int64_t value = 0;
  if (number) {
    CFNumberGetValue(number.get(), kCFNumberSInt64Type, &value);
  }
  return value;
}

auto names_list(ax::ui_element_view element, bool actions) -> std::string {
  cf::type value;
  const auto error = actions ? element.copy_action_names(value) : element.copy_attribute_names(value);
  if (error != kAXErrorSuccess || !value) {
    return {};
  }

  const auto array = value.cast<CFArrayRef>();
  if (!array) {
    return {};
  }

  std::string joined;
  const auto count = CFArrayGetCount(array.get());
  for (CFIndex index = 0; index < count; ++index) {
    const auto name = cf::array_at<CFStringRef>(array, index);
    if (!name) {
      continue;
    }

    if (!joined.empty()) {
      joined += ',';
    }
    joined += cf::string_view{name}.to_utf8().value_or("?");
  }

  return joined;
}

void print_frame(std::optional<CGRect> frame) {
  if (frame) {
    std::cout << " frame=(" << frame->origin.x << ',' << frame->origin.y << ' ' << frame->size.width
              << 'x' << frame->size.height << ')';
  }
}

// Prints `element` and, up to `max_depth` levels below `depth`, its subtree.
void dump_element(ax::ui_element_view element, std::size_t depth, std::size_t max_depth) {
  if (!element || depth > max_depth) {
    return;
  }

  const std::string indent(depth * 2, ' ');
  std::cout << indent << string_attribute(element, ax::role_attribute);
  if (const auto subrole = string_attribute(element, ax::subrole_attribute); !subrole.empty()) {
    std::cout << '/' << subrole;
  }
  if (const auto id = string_attribute(element, ax::identifier_attribute); !id.empty()) {
    std::cout << " id=\"" << id << '"';
  }
  if (const auto title = string_attribute(element, ax::title_attribute); !title.empty()) {
    std::cout << " title=\"" << title << '"';
  }
  if (const auto desc = string_attribute(element, ax::description_attribute); !desc.empty()) {
    std::cout << " desc=\"" << desc << '"';
  }
  print_frame(frame_attribute(element));

  cf::type wid_value;
  if (element.copy_attribute_value(cf::string_view{CFSTR("wid")}, wid_value) == kAXErrorSuccess &&
      wid_value) {
    if (const auto number = wid_value.cast<CFNumberRef>()) {
      std::int64_t wid = 0;
      CFNumberGetValue(number.get(), kCFNumberSInt64Type, &wid);
      std::cout << " wid=" << wid;
    }
  }

  bool settable = false;
  if (element.is_attribute_settable(ax::selected_attribute, settable) == kAXErrorSuccess) {
    std::cout << " selected-settable=" << std::boolalpha << settable;
  }
  if (element.is_attribute_settable(ax::focused_attribute, settable) == kAXErrorSuccess) {
    std::cout << " focused-settable=" << std::boolalpha << settable;
  }

  std::cout << "\n" << indent << "  attrs=[" << names_list(element, false) << "]";
  if (const auto actions = names_list(element, true); !actions.empty()) {
    std::cout << " actions=[" << actions << ']';
  }
  std::cout << '\n';

  cf::type value;
  if (element.copy_attribute_value(ax::children_attribute, value) != kAXErrorSuccess || !value) {
    return;
  }

  const auto children = value.cast<CFArrayRef>();
  if (!children) {
    return;
  }

  const auto count = CFArrayGetCount(children.get());
  for (CFIndex index = 0; index < count; ++index) {
    const auto child = cf::array_at<AXUIElementRef>(children, index);
    if (child) {
      dump_element(ax::ui_element_view{child}, depth + 1, max_depth);
    }
  }
}

auto cursor_location(cg::event_source_view source) -> CGPoint {
  return cg::event::create(source).location();
}

auto require_dock() -> pid_t {
  const auto pid = detail::dock_pid();
  if (!pid) {
    std::cerr << "Dock not found\n";
    std::exit(2);
  }

  const auto state = detail::detect_dock_view_state(*pid);
  std::cout << "dock pid=" << *pid << " state="
            << (state ? detail::dock_view_state_name(*state) : "unknown") << '\n';
  return *pid;
}

auto run_dump(int argc, char **argv) -> int {
  std::size_t max_depth = 10;
  bool window_manager = false;
  for (int index = 2; index < argc; ++index) {
    if (std::string_view{argv[index]} == "--wm") {
      window_manager = true;
    } else {
      max_depth = static_cast<std::size_t>(std::atoi(argv[index]));
    }
  }

  wait_for_start();
  auto pid = require_dock();
  if (window_manager) {
    const auto wm_pid = detail::window_manager_pid();
    if (!wm_pid) {
      std::cerr << "WindowManager not found\n";
      return 2;
    }
    std::cout << "WindowManager pid=" << *wm_pid << '\n';
    pid = *wm_pid;
  }

  dump_element(ax::ui_element::create_application(pid).view(), 0, max_depth);
  return 0;
}

using display_thumbnails = std::pair<detail::display_record, std::vector<detail::thumbnail_record>>;

// Lists the visible thumbnails on each display's current space through the
// core's enumeration.
auto list_visible_thumbnails() -> std::vector<display_thumbnails> {
  std::vector<display_thumbnails> result;
  const auto wm = detail::window_manager_pid();
  if (!wm) {
    std::cerr << "WindowManager not found\n";
    return result;
  }

  std::vector<detail::display_record> displays;
  if (!detail::load_active_displays(displays)) {
    return result;
  }

  for (const auto &display : displays) {
    const auto space_id = detail::current_space_id_for_display(display.uuid);
    std::vector<detail::thumbnail_record> all;
    const bool loaded = detail::load_thumbnails_for_display(*wm, display, all);
    auto visible = space_id ? detail::visible_thumbnails_in_reading_order(std::span{all}, *space_id)
                            : std::vector<detail::thumbnail_record>{};
    std::cout << "display " << display.display_id << " uuid=" << display.uuid
              << " space=" << (space_id ? std::to_string(*space_id) : "?")
              << " loaded=" << std::boolalpha << loaded << " total=" << all.size()
              << " visible=" << visible.size() << '\n';
    for (std::size_t index = 0; index < visible.size(); ++index) {
      std::cout << "  [" << index << "] wid=" << visible[index].window_id;
      print_frame(visible[index].frame);
      std::cout << '\n';
    }
    result.emplace_back(display, std::move(visible));
  }

  return result;
}

auto run_list() -> int {
  wait_for_start();
  (void)require_dock();
  (void)list_visible_thumbnails();
  return 0;
}

auto run_hit(int argc, char **argv) -> int {
  wait_for_start();
  (void)require_dock();
  const auto system_wide = ax::ui_element::create_system_wide();
  for (int index = 2; index + 1 < argc; index += 2) {
    const CGPoint point{std::atof(argv[index]), std::atof(argv[index + 1])};
    AXUIElementRef found = nullptr;
    const auto error = system_wide.view().copy_element_at_position(point, found);
    std::cout << "hit (" << point.x << ',' << point.y << ") error=" << error << '\n';

    auto element = found ? ax::ui_element::adopt(found) : ax::ui_element{};
    for (std::size_t depth = 0; element && depth < 8; ++depth) {
      dump_element(element.view(), depth, depth);  // this element only
      cf::type parent;
      if (element.view().copy_attribute_value(cf::string_view{kAXParentAttribute}, parent) !=
              kAXErrorSuccess ||
          !parent) {
        break;
      }
      const auto parent_ref = parent.cast<AXUIElementRef>();
      element = parent_ref ? ax::ui_element::retain(ax::ui_element_view{parent_ref.get()})
                           : ax::ui_element{};
    }
  }
  return 0;
}

// Hovers the INDEX-th visible thumbnail on DISPLAY. Mechanisms:
//   pid   mouse-moved posted to WindowManager's pid (does not highlight)
//   hop   the core's hover: hidden-cursor hop through the HID tap (default)
//   hide  like hop, spelled out here without the core
//   stay  warp there and leave the cursor until the hold ends (control)
auto run_hover_current(int argc, char **argv) -> int {
  if (argc < 4) {
    std::cerr << "usage: hover-current DISPLAY_ID INDEX [pid|hop|hide|stay]\n";
    return 2;
  }
  const auto display_id = static_cast<CGDirectDisplayID>(std::atoi(argv[2]));
  const auto index = static_cast<std::size_t>(std::atoi(argv[3]));
  const std::string_view mechanism = argc > 4 ? argv[4] : "hop";

  wait_for_start();
  (void)require_dock();
  for (const auto &[display, visible] : list_visible_thumbnails()) {
    if (display.display_id != display_id) {
      continue;
    }
    if (index >= visible.size()) {
      std::cerr << "index out of range\n";
      return 2;
    }

    const auto &frame = visible[index].frame;
    const CGPoint point{frame.origin.x + frame.size.width / 2, frame.origin.y + frame.size.height / 2};
    const auto source = cg::event_source::create(kCGEventSourceStateHIDSystemState);
    const auto before = cursor_location(source.view());
    auto event = cg::event::create_mouse(source.view(), kCGEventMouseMoved, point, kCGMouseButtonLeft);
    event.set_flags(0);

    bool posted = false;
    if (mechanism == "pid") {
      event.post_to_pid(*detail::window_manager_pid());
      posted = true;
    } else if (mechanism == "hop") {
      posted = detail::hover_thumbnail(source.view(), point);
    } else if (mechanism == "hide" || mechanism == "stay") {
      if (mechanism == "hide") {
        std::cout << "set_cursor_in_background=" << cgs::set_cursor_in_background(true)
                  << " hide=" << cg::hide_cursor() << '\n';
      }
      const auto moves_before = cg::hid_event_count(kCGEventMouseMoved);
      const auto started = std::chrono::steady_clock::now();
      (void)cg::warp_mouse_cursor_position(point);
      event.post(kCGHIDEventTap);
      const auto deadline = started + std::chrono::milliseconds{50};
      while (cg::hid_event_count(kCGEventMouseMoved) == moves_before &&
             std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
      }
      std::cout << "hop wait="
                << std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now() - started)
                       .count()
                << "us hid-delta=" << (cg::hid_event_count(kCGEventMouseMoved) - moves_before)
                << '\n';
      if (mechanism == "hide") {
        (void)cg::warp_mouse_cursor_position(before);
        std::cout << "show=" << cg::show_cursor() << '\n';
      }
      posted = true;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    const auto after = cursor_location(source.view());
    std::cout << "hovered index " << index << " via " << mechanism << " at (" << point.x << ','
              << point.y << ") posted=" << std::boolalpha << posted << " cursor before=("
              << before.x << ',' << before.y << ") after=(" << after.x << ',' << after.y << ")\n"
              << std::flush;
    hold();
    if (mechanism == "stay") {
      (void)cg::warp_mouse_cursor_position(before);
    }
    return 0;
  }

  std::cerr << "display not found\n";
  return 2;
}

// Triggers App Exposé for the Dock item titled TITLE (its AXShowExpose
// action), dumps the Dock state and WindowManager tree, then presses Escape.
auto run_expose(int argc, char **argv) -> int {
  if (argc < 3) {
    std::cerr << "usage: expose DOCK_ITEM_TITLE\n";
    return 2;
  }
  const std::string_view title{argv[2]};
  const auto dock_pid = detail::dock_pid();
  if (!dock_pid) {
    return 2;
  }

  ax::ui_element item;
  cf::type lists;
  const auto dock = ax::ui_element::create_application(*dock_pid);
  if (dock.view().copy_attribute_value(ax::children_attribute, lists) == kAXErrorSuccess && lists) {
    const auto list_array = lists.cast<CFArrayRef>();
    for (CFIndex i = 0; list_array && i < CFArrayGetCount(list_array.get()) && !item; ++i) {
      const auto list_ref = cf::array_at<AXUIElementRef>(list_array, i);
      cf::type items;
      if (!list_ref || ax::ui_element_view{list_ref}.copy_attribute_value(
                           ax::children_attribute, items) != kAXErrorSuccess ||
          !items) {
        continue;
      }
      const auto items_array = items.cast<CFArrayRef>();
      for (CFIndex j = 0; items_array && j < CFArrayGetCount(items_array.get()); ++j) {
        const auto item_ref = cf::array_at<AXUIElementRef>(items_array, j);
        if (item_ref && string_attribute(ax::ui_element_view{item_ref}, ax::title_attribute) == title) {
          item = ax::ui_element::retain(ax::ui_element_view{item_ref});
          break;
        }
      }
    }
  }
  if (!item) {
    std::cerr << "dock item not found\n";
    return 2;
  }

  std::cout << "AXShowExpose error="
            << item.view().perform_action(cf::string_view{CFSTR("AXShowExpose")}) << '\n';
  wait_for_start();
  (void)require_dock();
  if (const auto wm = detail::window_manager_pid()) {
    dump_element(ax::ui_element::create_application(*wm).view(), 0, 3);
  }
  (void)list_visible_thumbnails();

  const auto source = cg::event_source::create(kCGEventSourceStateHIDSystemState);
  cg::event::create_keyboard(source.view(), 53, true).post(kCGHIDEventTap);
  cg::event::create_keyboard(source.view(), 53, false).post(kCGHIDEventTap);
  return 0;
}

auto run_keys(int argc, char **argv) -> int {
  const int count = argc > 2 ? std::atoi(argv[2]) : 3;
  const int interval_ms = argc > 3 ? std::atoi(argv[3]) : 700;
  const bool shift = argc > 4 && std::string_view{argv[4]} == "--shift";

  wait_for_start();
  const auto source = cg::event_source::create(kCGEventSourceStateHIDSystemState);
  const CGEventFlags flags = kCGEventFlagMaskAlternate | (shift ? kCGEventFlagMaskShift : 0);
  for (int index = 0; index < count; ++index) {
    auto down = cg::event::create_keyboard(source.view(), 48, true);
    auto up = cg::event::create_keyboard(source.view(), 48, false);
    down.set_flags(flags);
    up.set_flags(flags);
    down.post(kCGHIDEventTap);
    up.post(kCGHIDEventTap);
    std::cout << "posted " << (shift ? "opt-shift-tab" : "opt-tab") << '\n' << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds{interval_ms});
  }
  return 0;
}

auto run_displays() -> int {
  std::vector<CGDirectDisplayID> displays;
  if (cg::active_displays(displays) != kCGErrorSuccess) {
    return 1;
  }
  for (const auto display : displays) {
    std::cout << "display " << display;
    print_frame(cg::display_bounds(display));
    std::cout << '\n';
  }
  const auto source = cg::event_source::create(kCGEventSourceStateHIDSystemState);
  const auto cursor = cursor_location(source.view());
  std::cout << "cursor=(" << cursor.x << ',' << cursor.y << ")\n";
  return 0;
}

auto run_spaces() -> int {
  const auto spaces = cgs::copy_managed_display_spaces(cgs::main_connection_id());
  if (!spaces) {
    return 1;
  }
  const auto description = cf::string::adopt(CFCopyDescription(spaces.get()));
  std::cout << description.to_utf8().value_or("?") << '\n';
  return 0;
}

auto run_windows() -> int {
  wait_for_start();
  const auto pid = require_dock();
  const auto info = cg::copy_window_info(
      static_cast<CGWindowListOption>(kCGWindowListOptionOnScreenOnly), kCGNullWindowID);
  if (!info) {
    return 1;
  }

  std::size_t app_windows = 0;
  const auto array = cf::view<CFArrayRef>{info.get()};
  const auto count = CFArrayGetCount(array.get());
  for (CFIndex index = 0; index < count; ++index) {
    const auto dict_ref = cf::array_at<CFDictionaryRef>(array, index);
    if (!dict_ref) {
      continue;
    }
    const auto dict = cf::dictionary_view{dict_ref};
    const auto layer = number_int64(dict, cg::window_layer_key);
    if (static_cast<pid_t>(number_int64(dict, cg::window_owner_pid_key)) == pid) {
      CGRect bounds{};
      if (const auto dictionary = dict.find<CFDictionaryRef>(cg::window_bounds_key)) {
        CGRectMakeWithDictionaryRepresentation(dictionary.get(), &bounds);
      }
      std::cout << "dock wid=" << number_int64(dict, cg::window_number_key) << " layer=" << layer;
      print_frame(bounds);
      std::cout << '\n';
    } else if (layer == 0) {
      ++app_windows;
    }
  }
  std::cout << "layer-0 app windows still on screen: " << app_windows << '\n';
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  const std::string_view command = argc > 1 ? argv[1] : "";
  if (command == "dump") {
    return run_dump(argc, argv);
  }
  if (command == "list") {
    return run_list();
  }
  if (command == "hit") {
    return run_hit(argc, argv);
  }
  if (command == "hover-current") {
    return run_hover_current(argc, argv);
  }
  if (command == "expose") {
    return run_expose(argc, argv);
  }
  if (command == "keys") {
    return run_keys(argc, argv);
  }
  if (command == "displays") {
    return run_displays();
  }
  if (command == "spaces") {
    return run_spaces();
  }
  if (command == "windows") {
    return run_windows();
  }

  std::cerr << "usage: mc_probe dump|list|hit|hover-current|expose|keys|displays|spaces|windows ...\n";
  return 2;
}
