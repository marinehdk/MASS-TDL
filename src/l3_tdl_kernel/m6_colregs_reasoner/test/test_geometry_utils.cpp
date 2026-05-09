#include <gtest/gtest.h>
#include "m6_colregs_reasoner/geometry_utils.hpp"

namespace mass_l3::m6_colregs {

TEST(GeometryUtilsTest, NormalizeBearingInRange) {
  const double result = normalize_bearing_deg(45.0);
  EXPECT_NEAR(result, 45.0, 1e-9);
}

TEST(GeometryUtilsTest, NormalizeBearingNegative) {
  const double result = normalize_bearing_deg(-45.0);
  EXPECT_NEAR(result, 315.0, 1e-9);
}

TEST(GeometryUtilsTest, NormalizeBearingAbove360) {
  const double result = normalize_bearing_deg(405.0);
  EXPECT_NEAR(result, 45.0, 1e-9);
}

TEST(GeometryUtilsTest, NormalizeBearingExact360) {
  const double result = normalize_bearing_deg(360.0);
  EXPECT_NEAR(result, 0.0, 1e-9);
}

TEST(GeometryUtilsTest, AngleDiffSameAngle) {
  const double result = angle_diff_deg(45.0, 45.0);
  EXPECT_NEAR(result, 0.0, 1e-9);
}

TEST(GeometryUtilsTest, AngleDiffOpposite) {
  const double result = angle_diff_deg(0.0, 180.0);
  EXPECT_NEAR(result, 180.0, 1e-9);
}

TEST(GeometryUtilsTest, AngleDiffAcrossZero) {
  const double result = angle_diff_deg(350.0, 10.0);
  EXPECT_NEAR(result, 20.0, 1e-9);
}

TEST(GeometryUtilsTest, AngleDiffSmall) {
  const double result = angle_diff_deg(90.0, 92.5);
  EXPECT_NEAR(result, 2.5, 1e-9);
}

TEST(GeometryUtilsTest, RelativeBearingHeadOn) {
  // Own heading 000, target bearing 000 => head-on
  const double result = relative_bearing_deg(0.0, 0.0);
  EXPECT_NEAR(result, 0.0, 1e-9);
}

TEST(GeometryUtilsTest, RelativeBearingStarboard) {
  // Own heading 000, target bearing 045 => 045 relative
  const double result = relative_bearing_deg(0.0, 45.0);
  EXPECT_NEAR(result, 45.0, 1e-9);
}

TEST(GeometryUtilsTest, RelativeBearingBehind) {
  // Own heading 090, target bearing 270 => 180 relative
  const double result = relative_bearing_deg(90.0, 270.0);
  EXPECT_NEAR(result, 180.0, 1e-9);
}

TEST(GeometryUtilsTest, RelativeBearingWrap) {
  // Own heading 350, target bearing 020 => 030 relative
  const double result = relative_bearing_deg(350.0, 20.0);
  EXPECT_NEAR(result, 30.0, 1e-9);
}

TEST(GeometryUtilsTest, AspectAngleFromBow) {
  // Own heading 000, target heading 180, relative bearing 000
  // Target is head-on: aspect = 180 - 0 - 0 + 180 = 360 -> normalized to 0
  const double result = aspect_angle_deg(0.0, 180.0, 0.0);
  EXPECT_NEAR(result, 0.0, 1e-9);
}

TEST(GeometryUtilsTest, AspectAngleStarboardSide) {
  // Own heading 000, target heading 270, relative bearing 045
  // aspect = 270 - 0 - 45 + 180 = 405 -> normalized to 45
  const double result = aspect_angle_deg(0.0, 270.0, 45.0);
  EXPECT_NEAR(result, 45.0, 1e-9);
}

TEST(GeometryUtilsTest, HaversineKnownDistance) {
  // Equator meridian: 1 degree lat ~= 111.195 km
  const double result = haversine_m(0.0, 0.0, 1.0, 0.0);
  EXPECT_NEAR(result, 111195.0, 500.0);
}

TEST(GeometryUtilsTest, HaversineZeroDistance) {
  const double result = haversine_m(52.0, 4.0, 52.0, 4.0);
  EXPECT_NEAR(result, 0.0, 1e-6);
}

TEST(GeometryUtilsTest, DegRadRoundtrip) {
  const double original_deg = 90.0;
  const double rad = deg2rad(original_deg);
  const double deg = rad2deg(rad);
  EXPECT_NEAR(original_deg, deg, 1e-9);
}

TEST(GeometryUtilsTest, DegRadKeyValues) {
  EXPECT_NEAR(deg2rad(0.0), 0.0, 1e-9);
  EXPECT_NEAR(deg2rad(180.0), M_PI, 1e-9);
  EXPECT_NEAR(rad2deg(0.0), 0.0, 1e-9);
  EXPECT_NEAR(rad2deg(M_PI), 180.0, 1e-9);
}

}  // namespace mass_l3::m6_colregs
