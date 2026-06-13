#include <gtest/gtest.h>

#include <cmath>

#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5 {
namespace {

// ---------------------------------------------------------------------------
// DEMO-2 P2: Geometric fallback turn magnitude from M6 min-alteration, not
// fixed 5/6 fraction. fallback_target_heading computes target_psi as
// route_bearing + min_alteration, clamped into the M4 heading window.
// ---------------------------------------------------------------------------

TEST(GeometricFallback, TargetsMinAlterationNotFixedFraction) {
  // Route bearing = 0° (north), window = [0°, 40°], min alteration = 20°.
  // Expected: 0° + 20° = 20°, inside window → no clamping.
  const double route_brg = 0.0;
  const double h_min = 0.0;
  const double h_max = 40.0 * M_PI / 180.0;
  const double min_alt = 20.0 * M_PI / 180.0;

  const double target = fallback_target_heading(route_brg, h_min, h_max, min_alt);

  EXPECT_NEAR(target, 20.0 * M_PI / 180.0, 1e-3);
}

TEST(GeometricFallback, ClampsToWindow) {
  // Min alteration larger than window → clamp at window edge.
  const double h_min = 0.0;
  const double h_max = 15.0 * M_PI / 180.0;
  const double min_alt = 30.0 * M_PI / 180.0;

  const double target = fallback_target_heading(0.0, h_min, h_max, min_alt);

  EXPECT_NEAR(target, h_max, 1e-3);
}

TEST(GeometricFallback, PortDirectionSubtractsMinAlteration) {
  const double route_brg = 0.0;
  const double h_min = -40.0 * M_PI / 180.0;
  const double h_max = 0.0;
  const double min_alt = 25.0 * M_PI / 180.0;

  const double target = fallback_target_heading(
      route_brg, h_min, h_max, min_alt, ColregsPreferredDirection::Port);

  EXPECT_NEAR(target, -25.0 * M_PI / 180.0, 1e-3);
}

TEST(GeometricFallback, ReduceSpeedKeepsRouteBearing) {
  const double route_brg = 10.0 * M_PI / 180.0;
  const double h_min = -40.0 * M_PI / 180.0;
  const double h_max = 40.0 * M_PI / 180.0;

  const double target = fallback_target_heading(
      route_brg, h_min, h_max, 30.0 * M_PI / 180.0,
      ColregsPreferredDirection::ReduceSpeed);

  EXPECT_NEAR(target, route_brg, 1e-3);
}

TEST(GeometricFallback, HoldKeepsRouteBearing) {
  const double route_brg = -5.0 * M_PI / 180.0;
  const double h_min = -40.0 * M_PI / 180.0;
  const double h_max = 40.0 * M_PI / 180.0;

  const double target = fallback_target_heading(
      route_brg, h_min, h_max, 30.0 * M_PI / 180.0,
      ColregsPreferredDirection::Hold);

  EXPECT_NEAR(target, route_brg, 1e-3);
}

TEST(GeometricFallback, WrappedWindowKeepsRouteForHoldAndReduceSpeed) {
  const double route_brg = 0.0;
  const double h_min = 335.0 * M_PI / 180.0;
  const double h_max = 5.0 * M_PI / 180.0;
  const double min_alt = 20.0 * M_PI / 180.0;

  const double hold_target = fallback_target_heading(
      route_brg, h_min, h_max, min_alt, ColregsPreferredDirection::Hold);
  const double reduce_target = fallback_target_heading(
      route_brg, h_min, h_max, min_alt, ColregsPreferredDirection::ReduceSpeed);

  EXPECT_NEAR(hold_target, route_brg, 1e-3);
  EXPECT_NEAR(reduce_target, route_brg, 1e-3);
}

TEST(GeometricFallback, WrappedWindowClampsPortAndStarboardToNearestBoundary) {
  const double route_brg = 0.0;
  const double h_min = 335.0 * M_PI / 180.0;
  const double h_max = 5.0 * M_PI / 180.0;
  const double min_alt = 30.0 * M_PI / 180.0;

  const double port_target = fallback_target_heading(
      route_brg, h_min, h_max, min_alt, ColregsPreferredDirection::Port);
  const double starboard_target = fallback_target_heading(
      route_brg, h_min, h_max, min_alt, ColregsPreferredDirection::Starboard);

  EXPECT_NEAR(port_target, h_min, 1e-3);
  EXPECT_NEAR(starboard_target, h_max, 1e-3);
}

TEST(GeometricFallback, DetectsWrappedHeadingWindowButExcludesBoundaryAndFullSpan) {
  EXPECT_FALSE(heading_window_is_wrapped(0.0, 0.0));
  EXPECT_FALSE(heading_window_is_wrapped(-M_PI, M_PI));
  EXPECT_FALSE(heading_window_is_wrapped(M_PI, -M_PI));
  EXPECT_FALSE(heading_window_is_wrapped(0.0, 2.0 * M_PI));
  EXPECT_FALSE(heading_window_is_wrapped(2.0 * M_PI, 0.0));

  EXPECT_TRUE(heading_window_is_wrapped(
      335.0 * M_PI / 180.0, 5.0 * M_PI / 180.0));
}

TEST(GeometricFallback, DetectsNegativeRouteWrappedHeadingWindow) {
  EXPECT_TRUE(heading_window_is_wrapped(
      -10.0 * M_PI / 180.0, -350.0 * M_PI / 180.0));
}

TEST(GeometricFallback, UsesCircularDistanceForZeroMinAlterationInWrappedWindow) {
  const double route_brg = -1.0 * M_PI / 180.0;
  const double h_min = 350.0 * M_PI / 180.0;
  const double h_max = 10.0 * M_PI / 180.0;

  const double min_alt = fallback_min_alteration_rad(route_brg, h_min, h_max, 0.0);

  EXPECT_NEAR(min_alt, 9.0 * M_PI / 180.0, 1e-3);
}

TEST(GeometricFallback, TrajectoryMustReachColregsTargetHeading) {
  std::vector<TrajectoryPoint> under_altered;
  for (int i = 0; i < 4; ++i) {
    TrajectoryPoint pt;
    pt.psi_rad = 35.0 * M_PI / 180.0;
    under_altered.push_back(pt);
  }

  std::vector<TrajectoryPoint> reaches_target = under_altered;
  reaches_target.back().psi_rad = 58.0 * M_PI / 180.0;

  const double target = 60.0 * M_PI / 180.0;
  const double tol = 5.0 * M_PI / 180.0;

  EXPECT_FALSE(trajectory_reaches_heading(under_altered, target, tol));
  EXPECT_TRUE(trajectory_reaches_heading(reaches_target, target, tol));
}

TEST(GeometricFallback, ZeroMinAlterationStillRequiresM4WindowTarget) {
  std::vector<TrajectoryPoint> under_altered;
  for (int i = 0; i < 4; ++i) {
    TrajectoryPoint pt;
    pt.psi_rad = 32.0 * M_PI / 180.0;
    under_altered.push_back(pt);
  }

  std::vector<TrajectoryPoint> reaches_target = under_altered;
  reaches_target.back().psi_rad = 58.0 * M_PI / 180.0;

  const double route_brg = 0.0;
  const double h_min = 0.0;
  const double h_max = 60.0 * M_PI / 180.0;
  const double zero_min_alt = 0.0;
  const double tol = 5.0 * M_PI / 180.0;

  EXPECT_FALSE(trajectory_reaches_colregs_target(
      under_altered,
      route_brg,
      h_min,
      h_max,
      zero_min_alt,
      ColregsPreferredDirection::Starboard,
      tol));
  EXPECT_TRUE(trajectory_reaches_colregs_target(
      reaches_target,
      route_brg,
      h_min,
      h_max,
      zero_min_alt,
      ColregsPreferredDirection::Starboard,
      tol));
}

TEST(GeometricFallback, DetectsM4GeometricFallbackRationale) {
  EXPECT_TRUE(is_m4_fallback_rationale(
      "IvP infeasible - geometric fallback ABSOLUTE"));
  EXPECT_TRUE(is_m4_fallback_rationale(
      "IvP infeasible - geometric fallback relative"));
  EXPECT_TRUE(is_m4_fallback_rationale("infeasible fallback"));
  EXPECT_TRUE(is_m4_fallback_rationale("Failsafe"));
  EXPECT_FALSE(is_m4_fallback_rationale("M4 TRANSIT - no avoidance required"));
}

TEST(GeometricFallback, TargetSpeedUsesPlannedSpeedBeforeNominalDefault) {
  EXPECT_NEAR(
      geometric_fallback_target_speed_kn(14.0 * 1852.0 / 3600.0, 10.0),
      14.0,
      1e-6);
  EXPECT_NEAR(geometric_fallback_target_speed_kn(0.0, 10.0), 10.0, 1e-6);
  EXPECT_NEAR(geometric_fallback_target_speed_kn(0.5, 10.0), 10.0, 1e-6);
}

TEST(GeometricFallback, FirstExecutableWaypointUsesSubstantialLookahead) {
  const double own_psi = 0.0;
  const double target_psi = 85.0 * M_PI / 180.0;
  const double speed_mps = 12.0 * 0.514444;
  const double rot_rad_s = 5.0 * M_PI / 180.0;

  const auto point = geometric_fallback_arc_point(
      own_psi, target_psi, speed_mps, rot_rad_s,
      geometric_fallback_waypoint_time_s(0));
  const double bearing = std::atan2(point.y_m, point.x_m);

  EXPECT_GE(bearing, 35.0 * M_PI / 180.0);
  EXPECT_LE(std::fabs(point.y_m), 500.0);
}

}  // namespace
}  // namespace mass_l3::m5
