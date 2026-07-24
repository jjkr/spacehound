#include <gtest/gtest.h>

#include <string_view>

#include <spacehound/version.hpp>

namespace {

TEST(library_tests, version_constants_match_version_string) {
  EXPECT_EQ(spacehound::version_major, 0U);
  EXPECT_EQ(spacehound::version_minor, 3U);
  EXPECT_EQ(spacehound::version_patch, 0U);
  EXPECT_EQ(spacehound::version, std::string_view{"0.3.0"});
  EXPECT_EQ(spacehound::version_string(), spacehound::version);

  if (spacehound::is_release_build) {
    EXPECT_TRUE(spacehound::version_build_metadata.empty());
    EXPECT_EQ(spacehound::version_full_string(), spacehound::version);
  } else {
    EXPECT_FALSE(spacehound::version_build_metadata.empty());
    EXPECT_EQ(
        spacehound::version_full_string().substr(0, spacehound::version.size()),
        spacehound::version);
    EXPECT_EQ(spacehound::version_full_string()[spacehound::version.size()], '+');
  }
}

}  // namespace
