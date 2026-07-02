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
// Own is far BEYOND the end of a short leg whose INFINITE-LINE extension passes
// near own. The perpendicular distance to the infinite line is small (own is
// near the line), but the actual nearest segment point is the far endpoint,
// which is far from own. The old fallback used the infinite-line distance and
// would wrongly accept this leg; the fixed fallback uses the clamped point
// distance and picks the genuinely-nearest segment.
//
// Setup: two legs meeting at a corner near own. leg0 goes north, leg1 goes east.
// Own is positioned so the infinite line of leg0 passes close, but leg0's actual
// endpoint is far — only the clamped distance reveals leg1 is the real nearest.
// ---------------------------------------------------------------------------
TEST(MidMpcRouteFrame, FallbackUsesEndClampedDistanceNotInfiniteLine) {
  // leg0: (0,0) -> (100, 0)  (short north leg, ends at (100,0)).
  // leg1: (100,0) -> (100, 1000)  (long east leg).
  // Own at (0,0) relative; own's TRUE position in world: at wp_n=0,wp_e=0, i.e.
  // at the leg0 start. But construct own RELATIVE so own is far past leg0's end
  // along its infinite line yet close to leg1's start.
  // Place own such that: own world ≈ (100, 5) → relative = own - wp[0]...
  // Simpler: own at (0,0) relative; world waypoints chosen so own is past leg0.
  // leg0 world: (-100, 5) -> (100, 5) → own relative frame: wp = leg0 - own.
  // own is at origin. leg0 starts at (-100,5) ends at (100,5): own at (0,0) is
  // INSIDE leg0 here — not a fallback case. To force fallback, own must be off
  // both segments. Use own past the route end.
  //
  // Route: leg0 (0,0)→(50,0) north, leg1 (50,0)→(50,50) east (an L). Own far
  // north-east, past both legs. Own relative = own - world_origin; place own
  // world at (200, 200): relative wp = world - own = (-200,-200),(-150,-200),...
  const std::vector<double> wp_n{-200.0, -150.0, -150.0};  // world 0,50,50 - own 200
  const std::vector<double> wp_e{-200.0, -200.0, -150.0};  // world 0,0,50 - own 200
  const auto proj = project_own_onto_polyline(wp_n, wp_e);
  ASSERT_TRUE(proj.valid);
  // Own (0,0) is past the route end. The nearest segment endpoint is leg1's end
  // (50,50) in world = (-150,-150) relative, distance = hypot(150,150)=212.1.
  // The nearest SEGMENT by clamped distance is leg1 (its end is closest). The
  // infinite-line distance to leg0 (a north line through x=-200) would be 200
  // (purely the east offset), which is SMALLER than leg1's clamped 212 — so the
  // OLD infinite-line fallback would wrongly pick leg0. The fix picks leg1.
  EXPECT_EQ(proj.leg_index, 1u)
      << "end-clamped fallback must pick the segment whose actual endpoint is "
      << "nearest (leg1), not the one whose infinite line passes nearest (leg0)";
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
