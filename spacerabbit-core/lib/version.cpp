#include <spacerabbit/version.hpp>

namespace spacerabbit {

auto version_string() noexcept -> std::string_view {
  return version;
}

auto version_full_string() noexcept -> std::string_view {
  return version_full;
}

}  // namespace spacerabbit
