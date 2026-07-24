#include <spacehound/version.hpp>

namespace spacehound {

auto version_string() noexcept -> std::string_view {
  return version;
}

auto version_full_string() noexcept -> std::string_view {
  return version_full;
}

}  // namespace spacehound
