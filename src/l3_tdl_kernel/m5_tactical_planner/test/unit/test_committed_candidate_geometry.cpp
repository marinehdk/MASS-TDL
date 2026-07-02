// test/unit/test_committed_candidate_geometry.cpp
// M1 review High-4/High-5: along-track frozen_prefix_count pure function
// (spec §6.6.2). Extracted from committed_candidate_from_plan so the guard
// window is unit-testable independently of the ROS node.
//
// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "m5_tactical_planner/committed_route/committed_candidate_geometry.hpp"

using mass_l3::m5::committed_route::compute_frozen_prefix_count;
using mass_l3::m5::committed_route::kMinFirstChangedDistance_m;

namespace {

// WGS84 helpers. 1 deg latitude ≈ 111195 m (6371000 * π/180). Use a north-going
// route so along-track station equals the north offset in metres (modulo coslat,
// which is ~1.0 near lat 0). Base near the equator so 1 deg lat == 1 deg lon in m.
constexpr double kMetersPerDegLat = 111194.93;  // 6371000 * π/180
constexpr double kBaseLat = 1.0;   // near equator: east-west ≈ north-south m/deg
constexpr double kBaseLon = 1.0;

// Build a north-going plan whose waypoints are spaced `step_m` apart, starting
// `first_wp_offset_m` north of own. own sits at (kBaseLat, kBaseLon).
std::pair<std::vector<double>, std::vector<double>> north_plan(
    double first_wp_offset_m, double step_m, std::size_t n) {
  std::vector<double> lat(n), lon(n);
  for (std::size_t i = 0u; i < n; ++i) {
    const double offset_m = first_wp_offset_m + step_m * static_cast<double>(i);
    lat[i] = kBaseLat + offset_m / kMetersPerDegLat;
    lon[i] = kBaseLon;
  }
  return {lat, lon};
}

}  // namespace

// ---------------------------------------------------------------------------
// TEST 1: own before the route start, two waypoints within 100 m ahead → both
// frozen. own at origin, wp0 at +50 m, wp1 at +90 m (both < 100 m ahead).
// frozen_prefix_count == 2.
// ---------------------------------------------------------------------------
TEST(CommittedCandidateGeometry, FreezesWaypointsWithinGuardAheadOfOwn) {
  auto [lat, lon] = north_plan(/*first_wp_offset_m=*/50.0, /*step_m=*/40.0, 2u);
  const auto r = compute_frozen_prefix_count(lat, lon, kBaseLat, kBaseLon);
  ASSERT_TRUE(r.valid);
  // own is 50 m before the route start (negative station). The projection
  // reports own_station as the end-clamped distance to the nearest waypoint;
  // the exact sign is implementation-defined, but it must be small and finite.
  EXPECT_TRUE(std::isfinite(r.own_station_m));
  EXPECT_EQ(r.frozen_prefix_count, 2u)
      << "wp0 at +50 m, wp1 at +90 m, both within 100 m ahead of own";
}

// ---------------------------------------------------------------------------
// TEST 2: a waypoint beyond the 100 m guard is NOT frozen. wp0 at +50 m, wp1 at
// +200 m. Only wp0 is within the guard → frozen_prefix_count == 1.
// ---------------------------------------------------------------------------
TEST(CommittedCandidateGeometry, DoesNotFreezeWaypointBeyondGuard) {
  auto [lat, lon] = north_plan(/*first_wp_offset_m=*/50.0, /*step_m=*/150.0, 2u);
  const auto r = compute_frozen_prefix_count(lat, lon, kBaseLat, kBaseLon);
  ASSERT_TRUE(r.valid);
  EXPECT_EQ(r.frozen_prefix_count, 1u)
      << "wp0 at +50 m is frozen; wp1 at +200 m is beyond the 100 m guard";
}

// ---------------------------------------------------------------------------
// TEST 3 (Critical High-4): along-track projection is own-station-relative.
// Build a route with waypoints at stations 0, 50, 150 (a clear gap so the 100 m
// guard from own_at_wp0 excludes wp2). own placed exactly at wp0 (own_station≈0):
//   guard window [.., 0+100=100] → wp0(0) and wp1(50) frozen, wp2(150) NOT.
// Then advance own 200 m so own_station≈200 and add wp3 at 400:
//   guard window [.., 300] → wp0(0),wp1(50),wp2(150) frozen, wp3(400) NOT.
// This asserts own_station advances monotonically with own and the window bound
// (own_station+guard), NOT the legacy absolute Euclidean distance, governs the
// frozen run.
// ---------------------------------------------------------------------------
TEST(CommittedCandidateGeometry, AlongTrackStationIsSignedAndBoundedByOwn) {
  // Route: wp0 at +0, wp1 at +50, wp2 at +150 (stations from route start).
  auto [lat, lon] = north_plan(/*first_wp_offset_m=*/0.0, /*step_m=*/50.0, 1u);
  lat.push_back(kBaseLat + 50.0 / kMetersPerDegLat);
  lon.push_back(kBaseLon);
  lat.push_back(kBaseLat + 150.0 / kMetersPerDegLat);
  lon.push_back(kBaseLon);
  ASSERT_EQ(lat.size(), 3u);

  // Own exactly at wp0 (own == wp0 position).
  const auto r0 = compute_frozen_prefix_count(lat, lon, lat[0], lon[0]);
  ASSERT_TRUE(r0.valid);
  EXPECT_NEAR(r0.own_station_m, 0.0, 1.0);
  // Window [.., 100]: wp0(0), wp1(50) ≤ 100; wp2(150) > 100 → count 2.
  EXPECT_EQ(r0.frozen_prefix_count, 2u)
      << "own at wp0: wp0(0)+wp1(50) within 100 m, wp2(150) beyond";

  // Advance own 200 m north, add wp3 at station 400.
  lat.push_back(kBaseLat + 400.0 / kMetersPerDegLat);
  lon.push_back(kBaseLon);
  const double own2_lat = kBaseLat + 200.0 / kMetersPerDegLat;
  const auto r1 = compute_frozen_prefix_count(lat, lon, own2_lat, kBaseLon);
  ASSERT_TRUE(r1.valid);
  EXPECT_NEAR(r1.own_station_m, 200.0, 2.0)
      << "own advanced 200 m along the route";
  // Window = 200 + 100 = 300. Waypoints at stations 0,50,150,400. wp0-2 ≤ 300;
  // wp3 at 400 > 300 → frozen_prefix_count == 3 (wp3 excluded).
  EXPECT_EQ(r1.frozen_prefix_count, 3u)
      << "own_station=200 → guard window [..,300]; wp3 at 400 must NOT be frozen";
}

// ---------------------------------------------------------------------------
// TEST 4: degenerate inputs — empty plan and a single waypoint.
// ---------------------------------------------------------------------------
TEST(CommittedCandidateGeometry, DegeneratePlanHandling) {
  // Empty plan.
  std::vector<double> empty_lat, empty_lon;
  const auto r_empty = compute_frozen_prefix_count(empty_lat, empty_lon,
                                                    kBaseLat, kBaseLon);
  EXPECT_FALSE(r_empty.valid);
  EXPECT_EQ(r_empty.frozen_prefix_count, 0u);

  // Single waypoint within 100 m ahead → frozen.
  std::vector<double> one_lat{kBaseLat + 40.0 / kMetersPerDegLat};
  std::vector<double> one_lon{kBaseLon};
  const auto r_near = compute_frozen_prefix_count(one_lat, one_lon,
                                                   kBaseLat, kBaseLon);
  ASSERT_TRUE(r_near.valid);
  EXPECT_EQ(r_near.frozen_prefix_count, 1u);

  // Single waypoint 250 m away → NOT frozen.
  std::vector<double> far_lat{kBaseLat + 250.0 / kMetersPerDegLat};
  std::vector<double> far_lon{kBaseLon};
  const auto r_far = compute_frozen_prefix_count(far_lat, far_lon,
                                                 kBaseLat, kBaseLon);
  ASSERT_TRUE(r_far.valid);
  EXPECT_EQ(r_far.frozen_prefix_count, 0u);
}

// ---------------------------------------------------------------------------
// TEST 5 (Critical High-4 discriminator vs legacy Euclidean): the legacy
// Euclidean guard froze any waypoint within 100 m of own REGARDLESS of whether
// own had overrun it. Construct a case where own is far along the route so the
// FIRST waypoint is far BEHIND own (> 100 m behind), making it clear the
// along-track bound — not the Euclidean distance — governs the window.
//
// wp0 at own-300 (300 m behind), wp1 at own+50, wp2 at own+120. own_station≈300.
// Euclidean dist(own,wp0)=300 m (> 100, so even legacy would not freeze it —
// not a discriminator here). The real discriminator is wp1 vs wp2: both are
// ~50-120 m ahead. Along-track window [.., 300+100=400]: wp0(0)≤400 counted,
// wp1(350)≤400 counted, wp2(420)>400 NOT counted → count 2. This asserts the
// window is own_station-relative, not absolute-from-route-start.
// ---------------------------------------------------------------------------
TEST(CommittedCandidateGeometry, GuardWindowIsRelativeToOwnStation) {
  // Route: wp0 at +0, wp1 at +350, wp2 at +420 (stations from route start).
  auto [lat, lon] = north_plan(/*first_wp_offset_m=*/0.0, /*step_m=*/350.0, 2u);
  lat.push_back(kBaseLat + 420.0 / kMetersPerDegLat);
  lon.push_back(kBaseLon);
  // own 300 m along the route.
  const double own_lat = kBaseLat + 300.0 / kMetersPerDegLat;
  const auto r = compute_frozen_prefix_count(lat, lon, own_lat, kBaseLon);
  ASSERT_TRUE(r.valid);
  EXPECT_NEAR(r.own_station_m, 300.0, 2.0);
  // Stations: 0, 350, 420. Window [.., 300+100=400]. wp0(0)≤400, wp1(350)≤400,
  // wp2(420)>400 → count 2. wp1 is only 50 m ahead of own and IS frozen; wp2 is
  // 120 m ahead (> 100 m guard) and is NOT. This is the own-relative window.
  EXPECT_EQ(r.frozen_prefix_count, 2u)
      << "wp1 (350, i.e. +50 m ahead) frozen; wp2 (420, i.e. +120 m ahead) not";
}
