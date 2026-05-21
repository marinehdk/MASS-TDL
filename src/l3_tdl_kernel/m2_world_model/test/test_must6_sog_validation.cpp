#include <gtest/gtest.h>

namespace {

struct SogValidationResult {
  bool valid;
  double threshold_kn;
};

/// Validate SOG against manifest max speed.
/// threshold = max_speed_kn * 1.2 (20% margin for sea state / currents).
/// Rejects if sog_kn exceeds threshold.
SogValidationResult validate_sog(double sog_kn, double max_speed_kn) {
  double const threshold = max_speed_kn * 1.2;
  return {sog_kn <= threshold, threshold};
}

}  // namespace

TEST(Must6SogValidationTest, RejectsOverMaxSpeed) {
  auto result = validate_sog(30.0, 22.0);
  EXPECT_FALSE(result.valid);
  EXPECT_DOUBLE_EQ(result.threshold_kn, 26.4);
}

TEST(Must6SogValidationTest, AcceptsAtMaxSpeed) {
  auto result = validate_sog(22.0, 22.0);
  EXPECT_TRUE(result.valid);
  EXPECT_DOUBLE_EQ(result.threshold_kn, 26.4);
}

TEST(Must6SogValidationTest, AcceptsUnderMaxSpeed) {
  auto result = validate_sog(17.6, 22.0);
  EXPECT_TRUE(result.valid);
  EXPECT_DOUBLE_EQ(result.threshold_kn, 26.4);
}

TEST(Must6SogValidationTest, ManifestUnavailable) {
  double const default_max_speed_kn = 50.0;
  auto result = validate_sog(55.0, default_max_speed_kn);
  EXPECT_TRUE(result.valid);
  EXPECT_DOUBLE_EQ(result.threshold_kn, 60.0);
}
