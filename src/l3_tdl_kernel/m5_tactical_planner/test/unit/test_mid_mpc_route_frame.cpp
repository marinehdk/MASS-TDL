// test/unit/test_mid_mpc_route_frame.cpp
// Slice R1 review round-2 Critical 3: active-leg nearest-leg search + end-clamped
// fallback + cross-leg corner guard (spec §4.1/§4.3). Pure geometry extracted from
// MidMpcNode::assemble_input_ into testable free functions.
//
// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "m5_tactical_planner/mid_mpc/mid_mpc_route_frame.hpp"

using mass_l3::m5::mid_mpc::ActiveLegProjection;
using mass_l3::m5::mid_mpc::CrossLegGuardResult;
using mass_l3::m5::mid_mpc::evaluate_cross_leg_guard;
using mass_l3::m5::mid_mpc::project_own_onto_polyline;

// ---------------------------------------------------------------------------
// TEST 1: nearest-leg search picks the segment own projects INSIDE.
//
// Polyline: leg0 north (0,0)→(1000,0), leg1 north (1000,0)→(2000,0). Own at
// (0,0) projects onto leg0 at station 0. The active leg must be leg0 (the one
// own is on), not leg1.
// ---------------------------------------------------------------------------
TEST(MidMpcRouteFrame, ProjectOwnPicksLegContainingOwnProjection) {
  // Two collinear north legs; own at origin is on leg0.
  const std::vector<double> wp_n{0.0, 1000.0, 2000.0};
  const std::vector<double> wp_e{0.0, 0.0, 0.0};
  const auto proj = project_own_onto_polyline(wp_n, wp_e);
  ASSERT_TRUE(proj.valid);
  EXPECT_EQ(proj.leg_index, 0u);
  EXPECT_NEAR(proj.leg_length_m, 1000.0, 1.0e-6);
  EXPECT_NEAR(proj.station_s0_m, 0.0, 1.0e-6);   // own at the start of leg0
  EXPECT_NEAR(proj.route_bearing_rad, 0.0, 1.0e-9);  // north
  EXPECT_NEAR(proj.cross_track_l0_m, 0.0, 1.0e-6);    // on the leg
}

// ---------------------------------------------------------------------------
// TEST 2: station s0 is the along-track projection of own onto the active leg,
// NOT 0. This is the value the cross-leg guard needs (Critical 3A).
//
// Own 700 m along a 1000 m north leg → station s0 = 700, in the back half. The
// cross-leg guard must use (active_len - s0) = 300 m as the remaining distance
// to the corner.
// ---------------------------------------------------------------------------
TEST(MidMpcRouteFrame, ProjectOwnReturnsStationAlongTrack) {
  // Own at (700, 0); leg0 (0,0)→(1000,0) north.
  const std::vector<double> wp_n{-700.0, 300.0, 1300.0};  // own-relative: own at (0,0)
  const std::vector<double> wp_e{0.0, 0.0, 0.0};
  const auto proj = project_own_onto_polyline(wp_n, wp_e);
  ASSERT_TRUE(proj.valid);
  EXPECT_EQ(proj.leg_index, 0u);
  EXPECT_NEAR(proj.leg_length_m, 1000.0, 1.0e-6);
  EXPECT_NEAR(proj.station_s0_m, 700.0, 1.0e-6)  // 700 m along leg0
      << "station s0 must be own's along-track projection (700 m), not 0";
}

// ---------------------------------------------------------------------------
// TEST 3 (Critical 3A): cross-leg guard uses REMAINING distance to corner
// (active_len - s0), NOT the full active_len. Own in the BACK half of a long
// leg, heading along the leg, with reach just past the corner must trip the
// guard. The old guard (full active_len) would MISS this (own is far from leg
// START, but close to the corner).
//
// Own at station 700 m of a 1000 m north leg → remaining to corner = 300 m.
// Own heading north (along leg), reach = 900 m → reach·1.0 = 900 > 300 → crosses.
// The old guard compared 900 > 1000 (full len) → FALSE → MISSES the corner.
// This test must FAIL on the old logic and PASS on the fixed logic.
// ---------------------------------------------------------------------------
TEST(MidMpcRouteFrame, CrossLegGuardUsesRemainingDistanceNotFullLength) {
  // Own at (700,0) relative; leg0 (0,0)→(1000,0) north; leg1 → (2000,0).
  const std::vector<double> wp_n{-700.0, 300.0, 1300.0};
  const std::vector<double> wp_e{0.0, 0.0, 0.0};
  const auto proj = project_own_onto_polyline(wp_n, wp_e);
  ASSERT_TRUE(proj.valid);
  ASSERT_NEAR(proj.station_s0_m, 700.0, 1.0e-6);

  // Own heading north (along the leg, along_proj = 1.0). reach = 900 m.
  // Remaining to corner = 1000 - 700 = 300. 900·1.0 > 300 → crosses.
  const auto guard = evaluate_cross_leg_guard(proj, 3u, 0.0, 900.0);
  ASSERT_TRUE(guard.evaluated);
  EXPECT_TRUE(guard.crosses_corner)
      << "own in the back half of the leg (s0=700, len=1000) with reach=900 must "
      << "trip the corner guard (remaining to corner = 300 < 900); the old guard "
      << "compared 900 > 1000 (full length) and falsely missed the corner";
}

// ---------------------------------------------------------------------------
// TEST 3b (Critical 3A complement): own at the START of a long leg, reach less
// than the remaining distance → must NOT trip the guard (no corner crossing).
// ---------------------------------------------------------------------------
TEST(MidMpcRouteFrame, CrossLegGuardDoesNotTripWhenReachShortOfCorner) {
  // Own at (0,0) relative; leg0 (0,0)→(1000,0) north; leg1 → (2000,0).
  const std::vector<double> wp_n{0.0, 1000.0, 2000.0};
  const std::vector<double> wp_e{0.0, 0.0, 0.0};
  const auto proj = project_own_onto_polyline(wp_n, wp_e);
  ASSERT_TRUE(proj.valid);
  ASSERT_NEAR(proj.station_s0_m, 0.0, 1.0e-6);

  // Own heading north, reach = 900 m. Remaining to corner = 1000 - 0 = 1000.
  // 900·1.0 = 900 < 1000 → does NOT cross.
  const auto guard = evaluate_cross_leg_guard(proj, 3u, 0.0, 900.0);
  ASSERT_TRUE(guard.evaluated);
  EXPECT_FALSE(guard.crosses_corner)
      << "reach (900) short of the corner (remaining 1000) must not trip";
}

// ---------------------------------------------------------------------------
// TEST 4 (Critical 3B): end-clamped fallback uses the END-CLAMPED point distance,
// NOT the infinite-line perpendicular distance.
//
// RED-GREEN DISCRIMINATING FIXTURE (R1 round-3 review): own's own-relative
// position is constructed so the segment whose INFINITE-LINE extension passes
// nearest to own (leg0, inf-line dist = 5 m) is DIFFERENT from the segment whose
// actual nearest endpoint is closest (leg1, clamped dist ≈ 70.7 m). The OLD
// infinite-line fallback minimises perpendicular distance and picks leg0 (the
// leg whose extension passes near own, but whose actual segment is 300 m away);
// the FIXED end-clamped fallback minimises the distance to the nearest segment
// point and picks leg1 (whose endpoint is genuinely nearest).
//
// Geometry (own-relative NED, own at origin (0,0)):
//   leg0: wp0=(5, 300) -> wp1=(5, 900)  — a north leg 5 m EAST of own. own's
//          perpendicular to its infinite line is just the 5 m east offset, but
//          own projects along it at -300 m (before the segment start), so the
//          nearest ACTUAL leg0 point is wp0=(5,300), dist = hypot(5,300)≈300 m.
//   leg1: wp1=(5, 900) -> wp2=(50, 50)  — a diagonal leg. own projects along it
//          well past wp2, so its nearest clamped point is wp2=(50,50), dist =
//          hypot(50,50)≈70.7 m. leg1's infinite-line distance is ~52.6 m.
//
// Per-leg distances at own=(0,0):
//                  infinite-line    end-clamped
//   leg0           5.0 m  (min✓)    300.0 m
//   leg1          52.6 m            70.7 m  (min✓)
// OLD logic → leg0 (inf-line min); FIXED logic → leg1 (clamped min). Asserting
// leg_index==1 therefore FAILS on the old infinite-line fallback and PASSES only
// on the end-clamped fallback — a genuine red→green gate.
// ---------------------------------------------------------------------------
TEST(MidMpcRouteFrame, FallbackUsesEndClampedDistanceNotInfiniteLine) {
  // own-relative NED waypoints (own at origin). See fixture comment above.
  const std::vector<double> wp_n{5.0, 5.0, 50.0};     // (5,300),(5,900),(50,50)
  const std::vector<double> wp_e{300.0, 900.0, 50.0};
  const auto proj = project_own_onto_polyline(wp_n, wp_e);
  ASSERT_TRUE(proj.valid);
  // leg1 must be selected: its end-clamped point (50,50) is the nearest segment
  // point to own (≈70.7 m). The OLD infinite-line fallback would pick leg0
  // (perpendicular to its infinite line is only the 5 m east offset) — a bug,
  // since leg0's actual segment starts 300 m away. This assertion fails on the
  // old logic (leg0) and passes only on the clamped fix (leg1).
  EXPECT_EQ(proj.leg_index, 1u)
      << "end-clamped fallback must pick the segment whose actual nearest point "
      << "is closest (leg1, clamped dist≈70.7 m), not the one whose infinite "
      << "line passes nearest (leg0, inf-line dist=5 m but segment 300 m away)";
}

// ---------------------------------------------------------------------------
// TEST 5: degenerate / tiny polyline handling.
// ---------------------------------------------------------------------------
TEST(MidMpcRouteFrame, DegeneratePolylineIsInvalid) {
  const std::vector<double> empty_n;
  const std::vector<double> empty_e;
  EXPECT_FALSE(project_own_onto_polyline(empty_n, empty_e).valid);

  const std::vector<double> single_n{0.0};
  const std::vector<double> single_e{0.0};
  EXPECT_FALSE(project_own_onto_polyline(single_n, single_e).valid);
}

// ---------------------------------------------------------------------------
// TEST 6: signed cross-track is positive starboard (east for a north leg).
// ---------------------------------------------------------------------------
TEST(MidMpcRouteFrame, CrossTrackPositiveStarboardForNorthLeg) {
  // North leg (0,0)→(1000,0): x=north, y=east. Own 50 m EAST of the leg →
  // own world north=0, east=50. Relative wp = world - own.
  const std::vector<double> wp_n{0.0, 1000.0};     // world 0,1000 - own north 0
  const std::vector<double> wp_e{-50.0, -50.0};    // world 0,0 - own east 50
  const auto proj = project_own_onto_polyline(wp_n, wp_e);
  ASSERT_TRUE(proj.valid);
  EXPECT_NEAR(proj.cross_track_l0_m, 50.0, 1.0e-6)
      << "own 50 m east of a north leg → cross-track +50 (starboard positive)";
}

// ---------------------------------------------------------------------------
// TEST 7: cross-leg guard not evaluated when no next leg exists.
// ---------------------------------------------------------------------------
TEST(MidMpcRouteFrame, CrossLegGuardNotEvaluatedWithoutNextLeg) {
  // Single leg, no next leg → guard cannot cross into anything.
  const std::vector<double> wp_n{0.0, 1000.0};
  const std::vector<double> wp_e{0.0, 0.0};
  const auto proj = project_own_onto_polyline(wp_n, wp_e);
  ASSERT_TRUE(proj.valid);
  const auto guard = evaluate_cross_leg_guard(proj, 2u, 0.0, 900.0);
  EXPECT_FALSE(guard.evaluated);
  EXPECT_FALSE(guard.crosses_corner);
}
