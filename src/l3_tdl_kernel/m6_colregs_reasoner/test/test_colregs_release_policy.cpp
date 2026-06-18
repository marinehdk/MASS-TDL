#include <gtest/gtest.h>

#include "m6_colregs_reasoner/colregs_release_policy.hpp"

namespace mass_l3::m6_colregs {
namespace {

TEST(ColregsReleasePolicy, BlocksGiveWayProjectionReleaseBeforeRangeGate) {
  EXPECT_FALSE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/925.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/150.0,
      /*reference_relative_bearing_abs_deg=*/40.0,
      GiveWayProjectionReleaseGate::CURRENT_ABAFT));
}

TEST(ColregsReleasePolicy, AllowsHeadOnProjectionReleaseAtCurrentAbaftGate) {
  EXPECT_TRUE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/926.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/150.0,
      /*reference_relative_bearing_abs_deg=*/40.0,
      GiveWayProjectionReleaseGate::CURRENT_ABAFT));
}

TEST(ColregsReleasePolicy, BlocksGiveWayProjectionReleaseWithoutSafeProjection) {
  EXPECT_FALSE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/false,
      /*range_m=*/3000.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/180.0,
      /*reference_relative_bearing_abs_deg=*/90.0,
      GiveWayProjectionReleaseGate::REFERENCE_CLEAR));
}

TEST(ColregsReleasePolicy, BlocksHeadOnProjectionReleaseBeforeCurrentAbaftGate) {
  EXPECT_FALSE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/3000.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/149.0,
      /*reference_relative_bearing_abs_deg=*/90.0,
      GiveWayProjectionReleaseGate::CURRENT_ABAFT));
}

TEST(ColregsReleasePolicy, BlocksGiveWayProjectionReleaseBeforeReferenceBowClearGate) {
  EXPECT_FALSE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/3000.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/180.0,
      /*reference_relative_bearing_abs_deg=*/39.0,
      GiveWayProjectionReleaseGate::REFERENCE_CLEAR));
}

TEST(ColregsReleasePolicy, BlocksCrossingReleaseBeforeTargetAbaftTheBeam) {
  // Rule 8(d)/15: target only 40 deg off the bow is still on the bow, not
  // past-and-clear (abaft the beam = 112.5 deg per Rule 13(b) + Rule 21(c)).
  // Must hold.
  EXPECT_FALSE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/3883.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/125.0,
      /*reference_relative_bearing_abs_deg=*/40.0,
      GiveWayProjectionReleaseGate::REFERENCE_CLEAR));
}

TEST(ColregsReleasePolicy, AllowsCrossingProjectionReleaseAfterReferenceClear) {
  // Target now abaft the beam along the reference heading (112.5 deg) at safe
  // range -- genuine past-and-clear, release allowed.
  EXPECT_TRUE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/3883.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/125.0,
      /*reference_relative_bearing_abs_deg=*/112.5,
      GiveWayProjectionReleaseGate::REFERENCE_CLEAR));
}

TEST(ColregsReleasePolicy, BlocksReferenceHeadingReleaseWhenReturnCpaUnsafe) {
  EXPECT_FALSE(give_way_reference_heading_release_safe(
      /*range_m=*/3143.0,
      /*bearing_deg=*/42.9,
      /*target_heading_deg=*/290.0,
      /*target_speed_kn=*/10.61,
      /*own_speed_kn=*/9.79,
      /*reference_heading_deg=*/0.0,
      /*cpa_safe_m=*/926.0));
}

// Regression for the rule15-cs early-release bug: the reference-heading CPA
// projection release must ALSO require the target to be abaft the beam (rel_brg
// >= 112.5 deg along the reference heading), same Rule 8(d)/13(b)+21(c) gate as
// the REFERENCE_CLEAR projection path. Without this, the OR branch in
// colregs_reasoner_node.cpp (give_way_reference_heading_release_ok) releases the
// encounter while the target is still on the bow (rel_brg ~37 deg) even though
// the sibling give_way_projection_release_safe(REFERENCE_CLEAR) path correctly
// blocks. Phase gate flags this as a Rule 8(d) violation.
TEST(ColregsReleasePolicy, BlocksReferenceHeadingReleaseBeforeTargetAbaftTheBeam) {
  // bearing=37.7 deg, reference_heading=0 deg -> rel_brg=37.7 deg, well short of
  // the 112.5 deg abaft-beam gate. CPA projection is safe (this was the
  // AllowsReferenceHeadingReleaseWhenReturnCpaSafe case before the fix). Must
  // hold (return false) because the target is still on the bow.
  EXPECT_FALSE(give_way_reference_heading_release_safe(
      /*range_m=*/2878.0,
      /*bearing_deg=*/37.7,
      /*target_heading_deg=*/290.0,
      /*target_speed_kn=*/10.61,
      /*own_speed_kn=*/9.88,
      /*reference_heading_deg=*/0.0,
      /*cpa_safe_m=*/926.0));
}

TEST(ColregsReleasePolicy, AllowsReferenceHeadingReleaseWhenReturnCpaSafe) {
  // Target now abaft the beam along the reference heading (bearing=150 deg vs
  // reference=0 deg -> rel_brg=150 deg > 112.5 deg) with a safe CPA projection.
  // Genuine past-and-clear, release allowed.
  EXPECT_TRUE(give_way_reference_heading_release_safe(
      /*range_m=*/2878.0,
      /*bearing_deg=*/150.0,
      /*target_heading_deg=*/290.0,
      /*target_speed_kn=*/10.61,
      /*own_speed_kn=*/9.88,
      /*reference_heading_deg=*/0.0,
      /*cpa_safe_m=*/926.0));
}

// Rule 13 overtake release now uses aspect-based give_way_overtake_release_safe
// (see BlocksOvertakeRelease*/AllowsOvertakeReleaseOnceAheadAndClear below);
// the crossing REFERENCE_CLEAR bow-clear gate is geometrically wrong for
// near-parallel overtaking courses and was removed from the rule13 path.

TEST(ColregsReleasePolicy, AllowsStandOnLateActionReleaseAtEmergencyCpaFloor) {
  EXPECT_TRUE(stand_on_late_action_release_safe(
      /*latched=*/true,
      /*range_closing=*/false,
      /*range_m=*/700.0,
      /*cpa_m=*/220.0,
      /*tcpa_s=*/0.0,
      /*configured_cpa_safe_m=*/500.0,
      /*current_relative_bearing_abs_deg=*/160.0));
}

TEST(ColregsReleasePolicy, AllowsStandOnLateActionReleaseAfterTcpPastWithoutAbaftBearing) {
  EXPECT_TRUE(stand_on_late_action_release_safe(
      /*latched=*/true,
      /*range_closing=*/false,
      /*range_m=*/492.0,
      /*cpa_m=*/492.0,
      /*tcpa_s=*/-0.5,
      /*configured_cpa_safe_m=*/500.0,
      /*current_relative_bearing_abs_deg=*/29.0));
}

TEST(ColregsReleasePolicy, BlocksStandOnLateActionReleaseBelowEmergencyCpaFloor) {
  EXPECT_FALSE(stand_on_late_action_release_safe(
      /*latched=*/true,
      /*range_closing=*/false,
      /*range_m=*/700.0,
      /*cpa_m=*/170.0,
      /*tcpa_s=*/0.0,
      /*configured_cpa_safe_m=*/500.0,
      /*current_relative_bearing_abs_deg=*/160.0));
}

TEST(ColregsReleasePolicy, BlocksStandOnLateActionReleaseInsideEmergencyRange) {
  EXPECT_FALSE(stand_on_late_action_release_safe(
      /*latched=*/true,
      /*range_closing=*/false,
      /*range_m=*/350.0,
      /*cpa_m=*/492.0,
      /*tcpa_s=*/-0.5,
      /*configured_cpa_safe_m=*/500.0,
      /*current_relative_bearing_abs_deg=*/29.0));
}

}  // namespace
}  // namespace mass_l3::m6_colregs

namespace mass_l3::m6_colregs {
namespace {

// ---------------------------------------------------------------------------
// COLREGs Rule 13(d) overtake release: past-and-clear geometry.
// An overtaking own-ship is "finally past and clear" only once it is ahead of
// the target along the target's heading -- not merely once the target has
// moved 40 deg off the own-ship's bow (the crossing/head-on bow-clear gate,
// which is geometrically meaningless for near-parallel overtaking courses
// where the target stays on the bow throughout the pass). Aspect angle is the
// right coordinate: 0 deg = own-ship dead ahead of the target, 180 deg = dead
// astern. Past-and-clear requires aspect < 90 deg (own-ship crossed the
// target's beam into its forward hemisphere) plus range/CPA safety.
// ---------------------------------------------------------------------------

TEST(ColregsReleasePolicy, BlocksOvertakeReleaseWhileStillAstern) {
  // Aspect 150 deg: own-ship still well astern of target -- not past yet.
  EXPECT_FALSE(give_way_overtake_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/1852.0,
      /*aspect_deg=*/150.0,
      /*cpa_safe_m=*/926.0));
}

TEST(ColregsReleasePolicy, BlocksOvertakeReleaseAtTargetBeam) {
  // Aspect 90 deg: own-ship exactly on the target's beam -- still passing,
  // not yet clear ahead.
  EXPECT_FALSE(give_way_overtake_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/1852.0,
      /*aspect_deg=*/90.0,
      /*cpa_safe_m=*/926.0));
}

TEST(ColregsReleasePolicy, BlocksOvertakeReleaseInsideSafeRange) {
  // Aspect 45 deg (ahead) but still inside cpa_safe range -- not clear.
  EXPECT_FALSE(give_way_overtake_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/800.0,
      /*aspect_deg=*/45.0,
      /*cpa_safe_m=*/926.0));
}

TEST(ColregsReleasePolicy, AllowsOvertakeReleaseOnceAheadAndClear) {
  // Aspect 45 deg (own-ship in target's forward hemisphere), range > cpa_safe,
  // CPA projection past and safe -- genuinely past and clear.
  EXPECT_TRUE(give_way_overtake_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/1852.0,
      /*aspect_deg=*/45.0,
      /*cpa_safe_m=*/926.0));
}

TEST(ColregsReleasePolicy, BlocksOvertakeReleaseWhenProjectionUnsafe) {
  // Ahead and far, but CPA projection not yet past/safe -- keep avoiding.
  EXPECT_FALSE(give_way_overtake_release_safe(
      /*cpa_projection_past_and_safe=*/false,
      /*range_m=*/1852.0,
      /*aspect_deg=*/45.0,
      /*cpa_safe_m=*/926.0));
}

}  // namespace
}  // namespace mass_l3::m6_colregs
