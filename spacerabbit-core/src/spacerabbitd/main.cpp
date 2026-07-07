#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <spacerabbit/daemon.hpp>
#include <spacerabbit/version.hpp>

namespace {

constexpr std::string_view usage =
    "usage: spacerabbitd [--help] [--version] [--settings <path>]";

auto print_version() -> void {
  std::cout << spacerabbit::version_full_string() << '\n' << std::flush;
}

auto print_help() -> void {
  std::cout
      << usage << "\n\n"
      << "Runs the supervised SpaceRabbit daemon in the foreground.\n"
      << std::flush;
}

}  // namespace

int main(int argc, char* argv[]) {
  spacerabbit::daemon::options options;

  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};

    if (argument == "--version") {
      if (argc != 2) {
        std::cerr << usage << '\n';
        return static_cast<int>(spacerabbit::daemon::exit_code::invalid_usage);
      }
      print_version();
      return 0;
    }

    if (argument == "--help") {
      if (argc != 2) {
        std::cerr << usage << '\n';
        return static_cast<int>(spacerabbit::daemon::exit_code::invalid_usage);
      }
      print_help();
      return 0;
    }

    if (argument == "--settings") {
      if (index + 1 >= argc || options.settings_path_override.has_value()) {
        std::cerr << usage << '\n';
        return static_cast<int>(spacerabbit::daemon::exit_code::invalid_usage);
      }

      options.settings_path_override = std::string{argv[index + 1]};
      ++index;
      continue;
    }

    std::cerr << usage << '\n';
    return static_cast<int>(spacerabbit::daemon::exit_code::invalid_usage);
  }

  return static_cast<int>(spacerabbit::daemon::run_foreground(options));
}
