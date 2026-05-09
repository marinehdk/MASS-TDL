#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <optional>

#include <Eigen/Core>

#include "m2_world_model/cpa_tcpa_calculator.hpp"
#include "m2_world_model/types.hpp"

namespace mass_l3::m2 {
namespace {

constexpr double kKnToMs = 0.514444;

OwnShipSnapshot make_own_ship(double lat, double lon, double sog_kn, double cog_deg,
                              double heading_deg, double u_water,
                              std::chrono::steady_clock::time_point stamp) {
  OwnShipSnapshot s;
  s.sog_kn = sog_kn;
  s.cog_deg = cog_deg;
  s.heading_deg = heading_deg;
  s.u_water = u_water;
  s.v_water = 0.0;
  s.current_speed_kn = 0.0;
  s.current_direction_deg = 0.0;
  s.latitude_deg = lat;
  s.longitude_deg = lon;
  s.covariance = Eigen::Matrix<double, 6, 6>::Zero();
  // Default position uncertainty: ~10 m std
  s.covariance(0, 0) = 100.0;  // east var (m^2)
  s.covariance(1, 1) = 100.0;  // north var (m^2)
  s.covariance(3, 3) = 1.0;    // ve var (m^2/s^2)
  s.covariance(4, 4) = 1.0;    // vn var (m^2/s^2)
  s.stamp = stamp;
  return s;
}

TargetSnapshot make_target(uint64_t id, double lat, double lon, double sog_kn,
                           double cog_deg, double heading_deg,
                           std::chrono::steady_clock::time_point stamp) {
  TargetSnapshot s;
  s.target_id = id;
  s.latitude_deg = lat;
  s.longitude_deg = lon;
  s.sog_kn = sog_kn;
  s.cog_deg = cog_deg;
  s.heading_deg = heading_deg;
  s.covariance = Eigen::Matrix<double, 3, 3>::Zero();
  s.covariance(0, 0) = 1e-10;  // lat var (deg^2)
  s.covariance(1, 1) = 1e-10;  // lon var (deg^2)
  s.covariance(2, 2) = 1.0;    // heading var (deg^2)
  s.classification = "unknown";
  s.classification_confidence = 0.5f;
  s.stamp = stamp;
  return s;
}

CpaTcpaCalculator::Config default_config() {
  return {CpaTcpaCalculator::UncertaintyMethod::Linear, 1000, 1.0, 2.0, 2.0, 0.5};
}

// Approx lat/lon scales
constexpr double kLatDegToM = 111320.0;

}  // namespace

// ──────────────────────────────────────────────
// Test 1: Head-on collision course → CPA ≈ 0
// ──────────────────────────────────────────────
TEST(CpaTcpaCalculatorTest, HeadOnCollisionCourse) {
  auto now = std::chrono::steady_clock::now();
  // Own ship at origin heading north at 10 kn
  double own_lat = 37.8;
  double own_lon = -122.4;
  double own_sog = 10.0;
  double own_cog = 0.0;

  // Target ~1 km ahead (north), heading south (reciprocal) at 10 kn
  double target_lat = own_lat + 1000.0 / kLatDegToM;
  double target_lon = own_lon;
  double target_sog = 10.0;
  double target_cog = 180.0;  // heading south

  auto own_ship = make_own_ship(own_lat, own_lon, own_sog, own_cog, 0.0, own_sog * kKnToMs, now);
  auto target = make_target(1, target_lat, target_lon, target_sog, target_cog, 180.0, now);

  CpaTcpaCalculator calc(default_config());
  auto result = calc.compute(own_ship, target, OddZone::B);

  ASSERT_TRUE(result.has_value());
  EXPECT_NEAR(result->cpa_m, 0.0, 1.0);   // CPA within 1 m
  EXPECT_GT(result->tcpa_s, 0.0);          // positive TCPA
  EXPECT_GT(result->uncertainty.cpa_sigma_m, 0.0);
}

// ──────────────────────────────────────────────
// Test 2: Safe pass — large CPA
// ──────────────────────────────────────────────
TEST(CpaTcpaCalculatorTest, SafePass) {
  auto now = std::chrono::steady_clock::now();
  double own_lat = 37.8;
  double own_lon = -122.4;

  // Target 2 km to the east, heading north (parallel track, well separated)
  double target_lat = own_lat;
  double target_lon = own_lon + 2000.0 / (kLatDegToM * std::cos(own_lat * M_PI / 180.0));
  double target_sog = 10.0;
  double target_cog = 0.0;  // heading north

  auto own_ship = make_own_ship(own_lat, own_lon, 10.0, 0.0, 0.0, 10.0 * kKnToMs, now);
  auto target = make_target(2, target_lat, target_lon, target_sog, target_cog, 0.0, now);

  CpaTcpaCalculator calc(default_config());
  auto result = calc.compute(own_ship, target, OddZone::B);

  ASSERT_TRUE(result.has_value());
  EXPECT_GT(result->cpa_m, 1800.0);   // CPA > 1800 m
  // Parallel tracks with equal speed: relative velocity = 0, TCPA = 0 (no convergence)
  EXPECT_GE(result->tcpa_s, 0.0);
}

// ──────────────────────────────────────────────
// Test 3: Overtaking scenario — faster target, same direction
// ──────────────────────────────────────────────
TEST(CpaTcpaCalculatorTest, OvertakingScenario) {
  auto now = std::chrono::steady_clock::now();
  double own_lat = 37.8;
  double own_lon = -122.4;

  // Target 500 m behind, heading same direction at higher speed
  double target_lat = own_lat - 500.0 / kLatDegToM;
  double target_lon = own_lon;

  auto own_ship = make_own_ship(own_lat, own_lon, 10.0, 0.0, 0.0, 10.0 * kKnToMs, now);
  auto target = make_target(3, target_lat, target_lon, 15.0, 0.0, 0.0, now);

  CpaTcpaCalculator calc(default_config());
  auto result = calc.compute(own_ship, target, OddZone::B);

  ASSERT_TRUE(result.has_value());
  // Target behind, faster → eventually catches up
  EXPECT_GT(result->tcpa_s, 0.0);
  EXPECT_EQ(result->cpa_m, result->cpa_m);  // not NaN
}

// ──────────────────────────────────────────────
// Test 4: Crossing scenario — perpendicular courses
// ──────────────────────────────────────────────
TEST(CpaTcpaCalculatorTest, CrossingScenario) {
  auto now = std::chrono::steady_clock::now();
  double own_lat = 37.8;
  double own_lon = -122.4;

  // Own ship heading north, target 500 m ahead and 500 m east, crossing west to east
  // Target heading east (cog=90)
  double d_north = 500.0 / kLatDegToM;
  double d_east = 500.0 / (kLatDegToM * std::cos(own_lat * M_PI / 180.0));
  double target_lat = own_lat + d_north;
  double target_lon = own_lon + d_east;

  auto own_ship = make_own_ship(own_lat, own_lon, 10.0, 0.0, 0.0, 10.0 * kKnToMs, now);
  auto target = make_target(4, target_lat, target_lon, 10.0, 90.0, 90.0, now);

  CpaTcpaCalculator calc(default_config());
  auto result = calc.compute(own_ship, target, OddZone::B);

  ASSERT_TRUE(result.has_value());
  EXPECT_GT(result->tcpa_s, 0.0);
  EXPECT_GT(result->cpa_m, 0.0);
}

// ──────────────────────────────────────────────
// Test 5: TCPA negative → returns current distance
// ──────────────────────────────────────────────
TEST(CpaTcpaCalculatorTest, TcpaNegativeReturnsCurrentDistance) {
  auto now = std::chrono::steady_clock::now();
  double own_lat = 37.8;
  double own_lon = -122.4;

  // Target already ahead and pulling away faster (negative TCPA → clamp to 0)
  double target_lat = own_lat + 500.0 / kLatDegToM;  // 500 m ahead (north)
  double target_lon = own_lon;

  // Target heading north at higher speed → rel_vel points north → TCPA < 0 → clamped to 0
  auto own_ship = make_own_ship(own_lat, own_lon, 10.0, 0.0, 0.0, 10.0 * kKnToMs, now);
  auto target = make_target(5, target_lat, target_lon, 15.0, 0.0, 0.0, now);

  CpaTcpaCalculator calc(default_config());
  auto result = calc.compute(own_ship, target, OddZone::B);

  ASSERT_TRUE(result.has_value());
  // TCPA should be 0 (clamped), CPA = current distance
  EXPECT_EQ(result->tcpa_s, 0.0);
  EXPECT_GT(result->cpa_m, 400.0);  // current separation
}

// ──────────────────────────────────────────────
// Test 6: Stationary target → TCPA = 0, CPA = current range
// ──────────────────────────────────────────────
TEST(CpaTcpaCalculatorTest, StationaryTarget) {
  auto now = std::chrono::steady_clock::now();
  double own_lat = 37.8;
  double own_lon = -122.4;

  double target_lat = own_lat + 200.0 / kLatDegToM;
  double target_lon = own_lon;

  auto own_ship = make_own_ship(own_lat, own_lon, 10.0, 0.0, 0.0, 10.0 * kKnToMs, now);
  auto target = make_target(6, target_lat, target_lon, 0.0, 0.0, 0.0, now);

  CpaTcpaCalculator calc(default_config());
  auto result = calc.compute(own_ship, target, OddZone::B);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->tcpa_s, 0.0);
  EXPECT_NEAR(result->cpa_m, 200.0, 1.0);
}

// ──────────────────────────────────────────────
// Test 7: Degraded uncertainty → larger sigma
// ──────────────────────────────────────────────
TEST(CpaTcpaCalculatorTest, DegradedUncertainty) {
  auto now = std::chrono::steady_clock::now();
  double own_lat = 37.8;
  double own_lon = -122.4;

  // Target 1 km north, crossing east-to-west
  double target_lat = own_lat + 1000.0 / kLatDegToM;
  double target_lon = own_lon;

  auto own_ship = make_own_ship(own_lat, own_lon, 10.0, 0.0, 0.0, 10.0 * kKnToMs, now);
  auto target = make_target(7, target_lat, target_lon, 10.0, 270.0, 270.0, now);

  // High covariance for own ship
  own_ship.covariance(0, 0) = 10000.0;   // 100 m std in position
  own_ship.covariance(1, 1) = 10000.0;
  own_ship.covariance(3, 3) = 100.0;     // 10 m/s std in velocity
  own_ship.covariance(4, 4) = 100.0;

  CpaTcpaCalculator calc(default_config());
  auto result = calc.compute(own_ship, target, OddZone::B);

  ASSERT_TRUE(result.has_value());
  EXPECT_GT(result->uncertainty.cpa_sigma_m, 1.0);
}

// ──────────────────────────────────────────────
// Test 8: Extrapolation time alignment
// ──────────────────────────────────────────────
TEST(CpaTcpaCalculatorTest, ExtrapolationAlignment) {
  auto now = std::chrono::steady_clock::now();
  double own_lat = 37.8;
  double own_lon = -122.4;

  // Target stamp = 200 ms before own_ship
  auto target_stamp = now - std::chrono::milliseconds(200);
  double target_lat = own_lat + 500.0 / kLatDegToM;
  double target_lon = own_lon;

  auto own_ship = make_own_ship(own_lat, own_lon, 10.0, 0.0, 0.0, 10.0 * kKnToMs, now);
  auto target = make_target(8, target_lat, target_lon, 10.0, 180.0, 180.0, target_stamp);

  CpaTcpaCalculator calc(default_config());
  auto result = calc.compute(own_ship, target, OddZone::B);

  ASSERT_TRUE(result.has_value());
  // Target should have been extrapolated forward ~1 m (10 kn ≈ 5 m/s * 0.2 s ≈ 1 m)
  EXPECT_GT(result->tcpa_s, 0.0);
}

// ──────────────────────────────────────────────
// Test 9: Static target detection (below speed threshold)
// ──────────────────────────────────────────────
TEST(CpaTcpaCalculatorTest, StaticTargetDetection) {
  auto now = std::chrono::steady_clock::now();
  double own_lat = 37.8;
  double own_lon = -122.4;

  double target_lat = own_lat + 300.0 / kLatDegToM;
  double target_lon = own_lon;

  auto own_ship = make_own_ship(own_lat, own_lon, 5.0, 0.0, 0.0, 5.0 * kKnToMs, now);
  auto target = make_target(9, target_lat, target_lon, 0.0, 0.0, 0.0, now);

  CpaTcpaCalculator::Config cfg = default_config();
  cfg.static_target_speed_mps = 1.0;  // 0 kn < 1 m/s threshold
  CpaTcpaCalculator calc(cfg);
  auto result = calc.compute(own_ship, target, OddZone::B);

  ASSERT_TRUE(result.has_value());
  // Stationary target → CPA = current range
  EXPECT_NEAR(result->cpa_m, 300.0, 2.0);
  EXPECT_EQ(result->tcpa_s, 0.0);
}

// ──────────────────────────────────────────────
// Test 10: ODD zone D safety factor scaling
// ──────────────────────────────────────────────
TEST(CpaTcpaCalculatorTest, OddZoneDSafetyFactor) {
  auto now = std::chrono::steady_clock::now();
  double own_lat = 37.8;
  double own_lon = -122.4;

  double target_lat = own_lat + 200.0 / kLatDegToM;
  double target_lon = own_lon;

  auto own_ship = make_own_ship(own_lat, own_lon, 10.0, 0.0, 0.0, 10.0 * kKnToMs, now);
  auto target = make_target(10, target_lat, target_lon, 0.0, 0.0, 0.0, now);

  // Zone D with odd_d_multiplier = 2.0 and safety_factor = 1.0
  CpaTcpaCalculator::Config cfg = default_config();
  cfg.safety_factor = 1.0;
  cfg.odd_d_multiplier = 2.0;
  CpaTcpaCalculator calc(cfg);
  auto result_d = calc.compute(own_ship, target, OddZone::D);

  ASSERT_TRUE(result_d.has_value());

  // Zone B for comparison (no multiplier)
  CpaTcpaCalculator calc2(cfg);
  auto result_b = calc2.compute(own_ship, target, OddZone::B);

  ASSERT_TRUE(result_b.has_value());
  EXPECT_NEAR(result_d->cpa_m, result_b->cpa_m * 2.0, 0.01);
}

// ──────────────────────────────────────────────
// Test 11: Extrapolation exceeds window → nullopt
// ──────────────────────────────────────────────
TEST(CpaTcpaCalculatorTest, ExtrapolationExceedsWindow) {
  auto now = std::chrono::steady_clock::now();
  double own_lat = 37.8;
  double own_lon = -122.4;

  // Target stamp = 10 s before (exceeds max_align_dt_s = 2.0)
  auto old_stamp = now - std::chrono::seconds(10);
  double target_lat = own_lat + 500.0 / kLatDegToM;
  double target_lon = own_lon;

  auto own_ship = make_own_ship(own_lat, own_lon, 10.0, 0.0, 0.0, 10.0 * kKnToMs, now);
  auto target = make_target(11, target_lat, target_lon, 10.0, 180.0, 180.0, old_stamp);

  CpaTcpaCalculator calc(default_config());
  auto result = calc.compute(own_ship, target, OddZone::B);

  EXPECT_FALSE(result.has_value());
}

// ──────────────────────────────────────────────
// Test 12: Zero relative velocity → stationary
// ──────────────────────────────────────────────
TEST(CpaTcpaCalculatorTest, ZeroRelativeVelocity) {
  auto now = std::chrono::steady_clock::now();
  double own_lat = 37.8;
  double own_lon = -122.4;

  double target_lat = own_lat + 400.0 / kLatDegToM;
  double target_lon = own_lon;

  // Both heading north at same speed
  auto own_ship = make_own_ship(own_lat, own_lon, 10.0, 0.0, 0.0, 10.0 * kKnToMs, now);
  auto target = make_target(12, target_lat, target_lon, 10.0, 0.0, 0.0, now);

  CpaTcpaCalculator calc(default_config());
  auto result = calc.compute(own_ship, target, OddZone::B);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->tcpa_s, 0.0);
  // CPA = current separation (400 m)
  EXPECT_NEAR(result->cpa_m, 400.0, 2.0);
}

}  // namespace mass_l3::m2
