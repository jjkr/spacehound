// SPDX-FileCopyrightText: 2026 Joe Kramer
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <vector>
#include <unistd.h>

#include <spacehound/ax.hpp>
#include <spacehound/cgs.hpp>
#include <spacehound/cg.hpp>
#include <spacehound/dispatch.hpp>
#include <spacehound/ns.hpp>
#include <spacehound/version.hpp>

int main() {
  // The smoke binary exercises the tiny public surface without requiring setup code.
  std::cout << spacehound::version_string() << '\n';

  const spacehound::cf::dictionary_entry trust_entries[] = {{
      .key = spacehound::ax::trusted_check_option_prompt,
      .value = spacehound::cf::type_view{kCFBooleanFalse},
  }};
  const auto trust_options = spacehound::cf::dictionary::create(trust_entries);

  std::cout << "ax_trusted=" << std::boolalpha
            << spacehound::ax::is_process_trusted() << '\n';
  std::cout << "ax_trusted(prompt=false)=" << std::boolalpha
            << spacehound::ax::is_process_trusted_with_options(trust_options.view())
            << '\n';

  const auto source =
      spacehound::cg::event_source::create(kCGEventSourceStateHIDSystemState);
  const auto event = spacehound::cg::event::create(source.view());
  const auto keyboard =
      spacehound::cg::event::create_keyboard(source.view(), static_cast<CGKeyCode>(6), true);
  const auto app = spacehound::ax::ui_element::create_application(getpid());
  const auto value = spacehound::ax::value::create(CGPointMake(1.0, 2.0));
  const auto workspace = spacehound::ns::workspace::shared();
  const auto connection = spacehound::cgs::main_connection_id();
  const auto active_display =
      spacehound::cgs::copy_active_menu_bar_display_identifier(connection);

  std::vector<CGDirectDisplayID> displays;
  (void)spacehound::cg::active_displays(displays);
  const auto dispatch_to_main = &spacehound::dispatch::to_main;

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
