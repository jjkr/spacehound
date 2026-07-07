#include <gtest/gtest.h>

#include <string_view>

#include <spacerabbit/version.hpp>

namespace {

TEST(library_tests, version_constants_match_version_string) {
  EXPECT_EQ(spacerabbit::version_major, 0U);
  EXPECT_EQ(spacerabbit::version_minor, 1U);
  EXPECT_EQ(spacerabbit::version_patch, 0U);
  EXPECT_EQ(spacerabbit::version, std::string_view{"0.1.0"});
  EXPECT_EQ(spacerabbit::version_string(), spacerabbit::version);

  if (spacerabbit::is_release_build) {
    EXPECT_TRUE(spacerabbit::version_build_metadata.empty());
    EXPECT_EQ(spacerabbit::version_full_string(), spacerabbit::version);
  } else {
    EXPECT_FALSE(spacerabbit::version_build_metadata.empty());
    EXPECT_EQ(
        spacerabbit::version_full_string().substr(0, spacerabbit::version.size()),
        spacerabbit::version);
    EXPECT_EQ(spacerabbit::version_full_string()[spacerabbit::version.size()], '+');
  }
}

}  // namespace
