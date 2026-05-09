#include <gtest/gtest.h>

#include <cmath>

#include "m2_world_model/coord_transform.hpp"

namespace mass_l3::m2 {
namespace {

constexpr double kOriginLat = 37.7749;   // San Francisco
constexpr double kOriginLon = -122.4194;
constexpr double kToleranceM = 1.0;      // 1 meter positional tolerance
constexpr double kKnToMs = 0.514444;

// Approximate conversion factor: 1 degree latitude ≈ 111320 m
constexpr double kLatM = 111320.0;
// 1 degree longitude at kOriginLat ≈ 111320 * cos(lat_rad) m
// This is computed in the test.

double deg_to_rad(double deg) {
  return deg * M_PI / 180.0;
}

}  // namespace

TEST(CoordTransformTest, WGS84ToENU_AtOrigin_ReturnsZero) {
  CoordTransform ct;
  ct.init(kOriginLat, kOriginLon);

  Eigen::Vector2d pos, vel;
  bool ok = ct.wgs84_to_enu(kOriginLat, kOriginLon, 0.0, 0.0, pos, vel);

  EXPECT_TRUE(ok);
  EXPECT_NEAR(pos(0), 0.0, 1e-9);  // east = 0
  EXPECT_NEAR(pos(1), 0.0, 1e-9);  // north = 0
  EXPECT_NEAR(vel(0), 0.0, 1e-9);  // vel east = 0
  EXPECT_NEAR(vel(1), 0.0, 1e-9);  // vel north = 0
}

TEST(CoordTransformTest, WGS84ToENU_RoundTrip_Reversible) {
  CoordTransform ct;
  ct.init(kOriginLat, kOriginLon);

  // A point roughly 1 km north and 1 km east of origin
  double test_lat = kOriginLat + 0.009;
  double test_lon = kOriginLon + 0.009;
  double test_sog = 10.0;  // 10 knots
  double test_cog = 45.0;  // northeast

  Eigen::Vector2d pos, vel;
  bool ok_fwd = ct.wgs84_to_enu(test_lat, test_lon, test_sog, test_cog, pos, vel);
  ASSERT_TRUE(ok_fwd);

  double lat_back = 0.0;
  double lon_back = 0.0;
  bool ok_rev = ct.enu_to_wgs84(pos(0), pos(1), lat_back, lon_back);
  ASSERT_TRUE(ok_rev);

  EXPECT_NEAR(lat_back, test_lat, 1e-9);
  EXPECT_NEAR(lon_back, test_lon, 1e-9);
}

TEST(CoordTransformTest, ForwardTransform_KnownOffset) {
  CoordTransform ct;
  ct.init(kOriginLat, kOriginLon);

  // Move ~0.01 deg north → should be approx 1113.2 m north, 0 east
  double test_lat = kOriginLat + 0.01;
  double test_lon = kOriginLon;

  Eigen::Vector2d pos, vel;
  bool ok = ct.wgs84_to_enu(test_lat, test_lon, 0.0, 0.0, pos, vel);
  ASSERT_TRUE(ok);

  EXPECT_NEAR(pos(0), 0.0, kToleranceM);             // east ~ 0
  EXPECT_NEAR(pos(1), 0.01 * kLatM, 10.0);            // north ~ 1113 m

  // Move ~0.01 deg east: lon degrees are shorter by cos(lat)
  test_lat = kOriginLat;
  test_lon = kOriginLon + 0.01;
  ok = ct.wgs84_to_enu(test_lat, test_lon, 0.0, 0.0, pos, vel);
  ASSERT_TRUE(ok);

  double expected_east = 0.01 * kLatM * std::cos(deg_to_rad(kOriginLat));
  EXPECT_NEAR(pos(0), expected_east, 10.0);           // east ~ scaled
  EXPECT_NEAR(pos(1), 0.0, kToleranceM);              // north ~ 0
}

TEST(CoordTransformTest, UninitializedTransform_ReturnsFalse) {
  CoordTransform ct;
  // NOT initialized

  Eigen::Vector2d pos, vel;
  bool ok = ct.wgs84_to_enu(kOriginLat, kOriginLon, 5.0, 90.0, pos, vel);
  EXPECT_FALSE(ok);
}

TEST(CoordTransformTest, UninitializedEnuToWgs84_ReturnsFalse) {
  CoordTransform ct;
  // NOT initialized

  double lat = 0.0, lon = 0.0;
  bool ok = ct.enu_to_wgs84(100.0, 200.0, lat, lon);
  EXPECT_FALSE(ok);
}

TEST(CoordTransformTest, ENUToWGS84_RoundTrip) {
  CoordTransform ct;
  ct.init(kOriginLat, kOriginLon);

  // Convert a known ENU offset to WGS84 and back
  double east_m = 500.0;
  double north_m = 1000.0;

  double lat = 0.0, lon = 0.0;
  bool ok_fwd = ct.enu_to_wgs84(east_m, north_m, lat, lon);
  ASSERT_TRUE(ok_fwd);

  Eigen::Vector2d pos, vel;
  bool ok_rev = ct.wgs84_to_enu(lat, lon, 0.0, 0.0, pos, vel);
  ASSERT_TRUE(ok_rev);

  EXPECT_NEAR(pos(0), east_m, kToleranceM);
  EXPECT_NEAR(pos(1), north_m, kToleranceM);
}

TEST(CoordTransformTest, MultiplePoints_Consistent) {
  CoordTransform ct;
  ct.init(kOriginLat, kOriginLon);

  // Three points: origin, north, northeast
  // Check that north component increases monotonically and east component is correct
  struct TestPoint {
    double lat, lon;
  };

  TestPoint pts[] = {
    {kOriginLat,              kOriginLon},
    {kOriginLat + 0.005,      kOriginLon},
    {kOriginLat + 0.010,      kOriginLon},
    {kOriginLat,              kOriginLon + 0.005},
    {kOriginLat,              kOriginLon + 0.010},
  };

  Eigen::Vector2d pos, vel;
  double prev_north = -1e9;
  for (const auto& pt : pts) {
    bool ok = ct.wgs84_to_enu(pt.lat, pt.lon, 0.0, 0.0, pos, vel);
    ASSERT_TRUE(ok);
    // Points with same latitude should have increasing north
    if (std::abs(pt.lon - kOriginLon) < 1e-12) {
      EXPECT_GT(pos(1), prev_north);
      prev_north = pos(1);
    }
  }

  // Origin itself should be (0,0)
  bool ok = ct.wgs84_to_enu(kOriginLat, kOriginLon, 0.0, 0.0, pos, vel);
  ASSERT_TRUE(ok);
  EXPECT_NEAR(pos(0), 0.0, 1e-9);
  EXPECT_NEAR(pos(1), 0.0, 1e-9);

  // Northeast point should have positive east and north
  ok = ct.wgs84_to_enu(kOriginLat + 0.005, kOriginLon + 0.005, 0.0, 0.0, pos, vel);
  ASSERT_TRUE(ok);
  EXPECT_GT(pos(0), 0.0);
  EXPECT_GT(pos(1), 0.0);
}

TEST(CoordTransformTest, VelocityVector_DueNorth) {
  CoordTransform ct;
  ct.init(kOriginLat, kOriginLon);

  // Due north at 10 knots
  double sog_kn = 10.0;
  double cog_deg = 0.0;

  Eigen::Vector2d pos, vel;
  bool ok = ct.wgs84_to_enu(kOriginLat, kOriginLon, sog_kn, cog_deg, pos, vel);
  ASSERT_TRUE(ok);

  double speed_ms = sog_kn * kKnToMs;
  EXPECT_NEAR(vel(0), 0.0, 1e-9);         // east component = 0
  EXPECT_NEAR(vel(1), speed_ms, 1e-9);    // north component = speed
}

TEST(CoordTransformTest, VelocityVector_DueEast) {
  CoordTransform ct;
  ct.init(kOriginLat, kOriginLon);

  // Due east at 10 knots
  double sog_kn = 10.0;
  double cog_deg = 90.0;

  Eigen::Vector2d pos, vel;
  bool ok = ct.wgs84_to_enu(kOriginLat, kOriginLon, sog_kn, cog_deg, pos, vel);
  ASSERT_TRUE(ok);

  double speed_ms = sog_kn * kKnToMs;
  EXPECT_NEAR(vel(0), speed_ms, 1e-9);    // east component = speed
  EXPECT_NEAR(vel(1), 0.0, 1e-9);         // north component = 0
}

}  // namespace mass_l3::m2
