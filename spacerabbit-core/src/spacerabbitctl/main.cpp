#include <charconv>
#include <iostream>
#include <string_view>
#include <system_error>

#include <spacerabbit/control.hpp>
#include <spacerabbit/version.hpp>

namespace {

constexpr std::string_view usage =
    "usage: spacerabbitctl [--help] [--version]\n"
    "       spacerabbitctl workspace left [--wrap]\n"
    "       spacerabbitctl workspace right [--wrap]\n"
    "       spacerabbitctl workspace goto <index> [--wrap]\n"
    "       spacerabbitctl window next\n"
    "       spacerabbitctl window prev\n"
    "       spacerabbitctl display left [--wrap]\n"
    "       spacerabbitctl display right [--wrap]\n"
    "       spacerabbitctl display goto <index> [--wrap]\n"
    "       spacerabbitctl mission-control toggle\n"
    "       spacerabbitctl expose toggle";

enum class exit_code {
  success = 0,
  invalid_usage = 1,
  runtime_error = 2,
};

auto print_version() -> void {
  std::cout << spacerabbit::version_full_string() << '\n' << std::flush;
}

auto print_help() -> void {
  std::cout
      << usage << "\n\n"
      << "Runs direct SpaceRabbit control actions from the command line.\n"
      << std::flush;
}

auto parse_wrap_flag(std::string_view argument, bool &wrap) -> bool {
  if (argument != "--wrap" || wrap) {
    return false;
  }

  wrap = true;
  return true;
}

auto parse_index(std::string_view argument, std::size_t &index) -> bool {
  const auto *first = argument.data();
  const auto *last = argument.data() + argument.size();
  const auto [ptr, parse_error] = std::from_chars(first, last, index);
  return parse_error == std::errc{} && ptr == last && index >= 1;
}

auto parse_workspace_command(
    int argc,
    char *argv[],
    spacerabbit::control::request &request) -> bool {
  if (argc < 3) {
    return false;
  }

  bool wrap = false;
  const std::string_view action{argv[2]};
  if (action == "left" || action == "right") {
    if (argc == 4) {
      if (!parse_wrap_flag(argv[3], wrap)) {
        return false;
      }
    } else if (argc != 3) {
      return false;
    }

    request = spacerabbit::control::workspace_request{
        .action = action == "left" ? spacerabbit::control::workspace_action::left
                                   : spacerabbit::control::workspace_action::right,
        .wrap = wrap,
    };
    return true;
  }

  if (action != "goto" || argc < 4 || argc > 5) {
    return false;
  }

  std::size_t index = 0;
  if (!parse_index(argv[3], index)) {
    return false;
  }

  if (argc == 5 && !parse_wrap_flag(argv[4], wrap)) {
    return false;
  }

  request = spacerabbit::control::workspace_request{
      .action = spacerabbit::control::workspace_action::go_to,
      .index = index,
      .wrap = wrap,
  };
  return true;
}

auto parse_display_command(
    int argc,
    char *argv[],
    spacerabbit::control::request &request) -> bool {
  if (argc < 3) {
    return false;
  }

  bool wrap = false;
  const std::string_view action{argv[2]};
  if (action == "left" || action == "right") {
    if (argc == 4) {
      if (!parse_wrap_flag(argv[3], wrap)) {
        return false;
      }
    } else if (argc != 3) {
      return false;
    }

    request = spacerabbit::control::display_request{
        .action = action == "left" ? spacerabbit::control::display_action::left
                                   : spacerabbit::control::display_action::right,
        .wrap = wrap,
    };
    return true;
  }

  if (action != "goto" || argc < 4 || argc > 5) {
    return false;
  }

  std::size_t index = 0;
  if (!parse_index(argv[3], index)) {
    return false;
  }

  if (argc == 5 && !parse_wrap_flag(argv[4], wrap)) {
    return false;
  }

  request = spacerabbit::control::display_request{
      .action = spacerabbit::control::display_action::go_to,
      .index = index,
      .wrap = wrap,
  };
  return true;
}

auto parse_window_command(
    int argc,
    char *argv[],
    spacerabbit::control::request &request) -> bool {
  if (argc != 3) {
    return false;
  }

  const std::string_view action{argv[2]};
  if (action == "next") {
    request = spacerabbit::control::window_focus_request{
        .direction = spacerabbit::control::window_focus_direction::next,
    };
    return true;
  }

  if (action == "prev") {
    request = spacerabbit::control::window_focus_request{
        .direction = spacerabbit::control::window_focus_direction::previous,
    };
    return true;
  }

  return false;
}

auto parse_command(int argc, char *argv[], spacerabbit::control::request &request) -> bool {
  if (argc < 3) {
    return false;
  }

  const std::string_view subject{argv[1]};
  if (subject == "workspace") {
    return parse_workspace_command(argc, argv, request);
  }

  if (subject == "display") {
    return parse_display_command(argc, argv, request);
  }

  if (subject == "window") {
    return parse_window_command(argc, argv, request);
  }

  if (subject == "mission-control" && argc == 3 && std::string_view{argv[2]} == "toggle") {
    request = spacerabbit::control::system_ui_request{
        .element = spacerabbit::control::system_ui_element::mission_control,
    };
    return true;
  }

  if (subject == "expose" && argc == 3 && std::string_view{argv[2]} == "toggle") {
    request = spacerabbit::control::system_ui_request{
        .element = spacerabbit::control::system_ui_element::expose,
    };
    return true;
  }

  return false;
}

}  // namespace

int main(int argc, char *argv[]) {
  if (argc == 2) {
    const std::string_view argument{argv[1]};
    if (argument == "--version") {
      print_version();
      return static_cast<int>(exit_code::success);
    }

    if (argument == "--help") {
      print_help();
      return static_cast<int>(exit_code::success);
    }
  }

  spacerabbit::control::request request;
  if (!parse_command(argc, argv, request)) {
    std::cerr << usage << '\n';
    return static_cast<int>(exit_code::invalid_usage);
  }

  const auto result = spacerabbit::control::execute(request);
  if (!result) {
    std::cerr << result.error().message << '\n';
    return static_cast<int>(exit_code::runtime_error);
  }

  return static_cast<int>(exit_code::success);
}
