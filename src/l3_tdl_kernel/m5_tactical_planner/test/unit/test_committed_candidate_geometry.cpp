// test/unit/test_committed_candidate_geometry.cpp
// M1 review High-4/High-5 + round-2 fix: along-track frozen_prefix_count pure
// function (spec §6.6.2). Extracted from committed_candidate_from_plan so the
// guard window is unit-testable independently of the ROS node.
//
// Round-2 (spec §6.6 prune crossed points): frozen_prefix_count counts ONLY
// waypoints strictly AHEAD of own within the in-guard window
// (own_station < station <= own_station + guard). Waypoints own has already
// overrun (station <= own_station) are pruned — they are executed geometry, not
// a frozen prefix. Therefore as own advances, the count shrinks or stays the
// same, never grows from an overrun waypoint (Critical High-4 round-2).
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
// TEST 3 (Critical High-4 round-2 prune): along-track projection is own-station-
// relative AND overrun waypoints are pruned. Route waypoints at stations 0, 50,
// 150 (then 400 added). own placed exactly at wp0 (own_station≈0):
//   AHEAD window (own_station, own_station+100] = (0, 100] → wp1(50) frozen;
//   wp0(0) is NOT strictly ahead (own is AT it → already executing) → pruned;
//   wp2(150) > 100 → not frozen. frozen_prefix_count == 1.
// Then advance own 200 m north (own_station≈200) and add wp3 at 400:
//   AHEAD window (200, 300]: wp0(0)/wp1(50)/wp2(150) all station <= 200 →
//   OVERRUN, pruned; wp3(400) > 300 → not frozen. frozen_prefix_count == 0.
// This asserts own_station advances monotonically with own, the forward bound
// (own_station+guard) governs the window, AND overrun waypoints are pruned as
// own advances — so the count drops (2→1→0), it never grows from an overrun.
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
  // AHEAD window (0, 100]: wp0(0) pruned (own AT it); wp1(50) frozen; wp2(150)
  // beyond → frozen_prefix_count == 1.
  EXPECT_EQ(r0.frozen_prefix_count, 1u)
      << "own at wp0: wp0 pruned (own on it), wp1(50) frozen, wp2(150) beyond";

  // Advance own 200 m north, add wp3 at station 400.
  lat.push_back(kBaseLat + 400.0 / kMetersPerDegLat);
  lon.push_back(kBaseLon);
  const double own2_lat = kBaseLat + 200.0 / kMetersPerDegLat;
  const auto r1 = compute_frozen_prefix_count(lat, lon, own2_lat, kBaseLon);
  ASSERT_TRUE(r1.valid);
  EXPECT_NEAR(r1.own_station_m, 200.0, 2.0)
      << "own advanced 200 m along the route";
  // AHEAD window (200, 300]: wp0-2 all station <= 200 → OVERRUN pruned; wp3(400)
  // > 300 → not frozen. frozen_prefix_count == 0 (everything overrun/beyond).
  EXPECT_EQ(r1.frozen_prefix_count, 0u)
      << "own_station=200: wp0-2 overrun pruned, wp3 at 400 beyond 300 window";
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
// TEST 5 (Critical High-4 round-2 prune discriminator): own is far along the
// route so wp0 is far BEHIND own (station 0 < own_station 300 → overrun). The
// ahead window (own_station, own_station+guard] = (300, 400]:
//   wp0(0)  → station <= own_station → OVERRUN, pruned;
//   wp1(350)→ 300 < 350 <= 400 → frozen (50 m ahead, in guard);
//   wp2(420)→ 420 > 400 → beyond the 100 m guard, not frozen.
// frozen_prefix_count == 1. This is the prune contract: a waypoint 50 m AHEAD
// is frozen while the far-behind wp0 is dropped, proving the window is own-
// station-relative AND that overrun waypoints are excluded (not the legacy
// absolute-Euclidean / route-start-relative count).
// ---------------------------------------------------------------------------
TEST(CommittedCandidateGeometry, GuardWindowIsRelativeToOwnStationAndPrunesOverrun) {
  // Route: wp0 at +0, wp1 at +350, wp2 at +420 (stations from route start).
  auto [lat, lon] = north_plan(/*first_wp_offset_m=*/0.0, /*step_m=*/350.0, 2u);
  lat.push_back(kBaseLat + 420.0 / kMetersPerDegLat);
  lon.push_back(kBaseLon);
  // own 300 m along the route.
  const double own_lat = kBaseLat + 300.0 / kMetersPerDegLat;
  const auto r = compute_frozen_prefix_count(lat, lon, own_lat, kBaseLon);
  ASSERT_TRUE(r.valid);
  EXPECT_NEAR(r.own_station_m, 300.0, 2.0);
  // Ahead window (300, 400]: wp0(0) overrun pruned; wp1(350) frozen; wp2(420)
  // beyond → frozen_prefix_count == 1.
  EXPECT_EQ(r.frozen_prefix_count, 1u)
      << "wp1 (350, +50 m ahead) frozen; wp0(0) overrun + wp2(420) beyond pruned";
}

// ---------------------------------------------------------------------------
// TEST 6 (Critical High-4 round-2 — the prune direction discriminator): the
// defining property of spec §6.6 is that as own ADVANCES, overrun waypoints are
// PRUNED, so frozen_prefix_count DECREASES (or stays equal), never grows from an
// overrun waypoint.
//
// Route with waypoints at along-track stations 0, 100, 200 (route start at own's
// initial base, so own_station is the clean north offset):
//   own at station 0  → ahead window (0,100]: wp0(0) own-on-it pruned,
//                        wp1(100) frozen, wp2(200) beyond → count 1.
//   own at station 150→ wp0(0) + wp1(100) overrun (<=150) pruned; wp2(200) is
//                        in (150,250] → frozen → count 1 (wp1 dropped, wp2 added).
//   own at station 250→ wp0-2 all overrun (<=250) pruned → count 0.
// The count sequence 1 → 1 → 0 NEVER grows above the initial 1 from an overrun
// waypoint; specifically the transition 150→250 is a strict decrease (prune).
// ---------------------------------------------------------------------------
TEST(CommittedCandidateGeometry, PrunesOverrunWaypointsAsOwnAdvances) {
  // Route start at base; waypoints at stations 0, 100, 200 north of base.
  auto [lat, lon] = north_plan(/*first_wp_offset_m=*/0.0, /*step_m=*/100.0, 3u);
  ASSERT_EQ(lat.size(), 3u);

  // own at route start (own == wp0). own_station≈0.
  const auto r0 = compute_frozen_prefix_count(lat, lon, kBaseLat, kBaseLon);
  ASSERT_TRUE(r0.valid);
  EXPECT_NEAR(r0.own_station_m, 0.0, 1.0);
  // Ahead window (0, 100]: wp0(0) pruned (own on it); wp1(100) frozen;
  // wp2(200) beyond → count 1.
  EXPECT_EQ(r0.frozen_prefix_count, 1u)
      << "own at wp0: wp0 pruned (own on it), wp1(+100) frozen, wp2(+200) beyond";

  // Advance own 150 m north (own_station≈150). wp0(0) and wp1(100) overrun
  // (station <= 150) → pruned; wp2(200) is in (150, 250] → frozen → count 1.
  const double own_150_lat = kBaseLat + 150.0 / kMetersPerDegLat;
  const auto r1 = compute_frozen_prefix_count(lat, lon, own_150_lat, kBaseLon);
  ASSERT_TRUE(r1.valid);
  EXPECT_NEAR(r1.own_station_m, 150.0, 2.0)
      << "own advanced 150 m along the route";
  EXPECT_EQ(r1.frozen_prefix_count, 1u)
      << "own at 150: wp0/wp1 overrun pruned, wp2(200) in (150,250] frozen";

  // Advance own a further 100 m (own physically at +250 m). wp0,wp1,wp2 all
  // overrun (station <= 250) → pruned; ahead window is empty → count 0. This is
  // the STRICT PRUNE: count drops 1 → 0 as own overruns wp2.
  // NOTE: own is now PAST the route end (wp2 at +200), so the projection's
  // end-clamped fallback reports own_station≈200 (the route-end station), not
  // 250. own_station is by construction clamped to [0, total_route_length]; the
  // prune contract still holds because no waypoint is strictly ahead of a
  // route-end own_station.
  const double own_250_lat = kBaseLat + 250.0 / kMetersPerDegLat;
  const auto r2 = compute_frozen_prefix_count(lat, lon, own_250_lat, kBaseLon);
  ASSERT_TRUE(r2.valid);
  EXPECT_NEAR(r2.own_station_m, 200.0, 2.0)
      << "own past route end → own_station end-clamped to route-end station 200";
  EXPECT_EQ(r2.frozen_prefix_count, 0u)
      << "own past all waypoints: all overrun → pruned, count must DROP to 0";

  // Assert the prune direction explicitly: advancing own never INCREASES the
  // count (it is non-increasing across own_station growth).
  EXPECT_LE(r1.frozen_prefix_count, r0.frozen_prefix_count);
  EXPECT_LE(r2.frozen_prefix_count, r1.frozen_prefix_count);
}
