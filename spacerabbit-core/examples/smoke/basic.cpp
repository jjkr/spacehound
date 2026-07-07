#include <iostream>
#include <vector>
#include <unistd.h>

#include <spacerabbit/ax.hpp>
#include <spacerabbit/cgs.hpp>
#include <spacerabbit/cg.hpp>
#include <spacerabbit/dispatch.hpp>
#include <spacerabbit/ns.hpp>
#include <spacerabbit/version.hpp>

int main() {
  // The smoke binary exercises the tiny public surface without requiring setup code.
  std::cout << spacerabbit::version_string() << '\n';

  const spacerabbit::cf::dictionary_entry trust_entries[] = {{
      .key = spacerabbit::ax::trusted_check_option_prompt,
      .value = spacerabbit::cf::type_view{kCFBooleanFalse},
  }};
  const auto trust_options = spacerabbit::cf::dictionary::create(trust_entries);

  std::cout << "ax_trusted=" << std::boolalpha
            << spacerabbit::ax::is_process_trusted() << '\n';
  std::cout << "ax_trusted(prompt=false)=" << std::boolalpha
            << spacerabbit::ax::is_process_trusted_with_options(trust_options.view())
            << '\n';

  const auto source =
      spacerabbit::cg::event_source::create(kCGEventSourceStateHIDSystemState);
  const auto event = spacerabbit::cg::event::create(source.view());
  const auto keyboard =
      spacerabbit::cg::event::create_keyboard(source.view(), static_cast<CGKeyCode>(6), true);
  const auto app = spacerabbit::ax::ui_element::create_application(getpid());
  const auto value = spacerabbit::ax::value::create(CGPointMake(1.0, 2.0));
  const auto workspace = spacerabbit::ns::workspace::shared();
  const auto connection = spacerabbit::cgs::main_connection_id();
  const auto active_display =
      spacerabbit::cgs::copy_active_menu_bar_display_identifier(connection);

  std::vector<CGDirectDisplayID> displays;
  (void)spacerabbit::cg::active_displays(displays);
  const auto dispatch_to_main = &spacerabbit::dispatch::to_main;

  // Printing the wrapper truthiness is enough to verify Core Graphics object creation.
  std::cout << "cg_source=" << static_cast<bool>(source)
            << " cg_event=" << static_cast<bool>(event)
            << " cg_keyboard=" << static_cast<bool>(keyboard)
            << " ax_app=" << static_cast<bool>(app)
            << " ax_value=" << static_cast<bool>(value)
            << " ns_workspace=" << static_cast<bool>(workspace)
            << " cgs_active_display=" << static_cast<bool>(active_display)
            << " dispatch_helper=" << (dispatch_to_main != nullptr)
            << '\n';
  return 0;
}
