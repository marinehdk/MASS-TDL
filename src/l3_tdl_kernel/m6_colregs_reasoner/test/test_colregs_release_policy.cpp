#include <gtest/gtest.h>

#include "m6_colregs_reasoner/colregs_release_policy.hpp"
#include "m6_colregs_reasoner/types.hpp"

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

TEST(ColregsReleasePolicy, BlocksCrossingProjectionReleaseBeforeBeam) {
  // 40° (old quick-impl threshold) must now be blocked: target still on the
  // bow, not past the 90° beam. The 112.5° abaft-beam (Rule 13(b) overtaking
  // sector) is unreachable for shallow slow crossings; 90° beam is the
  // corrected crossing/head-on threshold.
  EXPECT_FALSE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/3883.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/125.0,
      /*reference_relative_bearing_abs_deg=*/40.0,
      GiveWayProjectionReleaseGate::REFERENCE_CLEAR));
}

TEST(ColregsReleasePolicy, AllowsCrossingProjectionReleasePastBeam) {
  // Past the 90° beam: target reference rel bearing 95°.
  EXPECT_TRUE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/3883.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/125.0,
      /*reference_relative_bearing_abs_deg=*/95.0,
      GiveWayProjectionReleaseGate::REFERENCE_CLEAR));
}

TEST(ColregsReleasePolicy, BlocksCrossingProjectionReleaseAtExactlyBeam) {
  // Exactly 90° is not past the beam; require strictly greater.
  EXPECT_FALSE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/3883.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/125.0,
      /*reference_relative_bearing_abs_deg=*/90.0,
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

TEST(ColregsReleasePolicy, BlocksReferenceHeadingReleaseBeforeTargetPastBeam) {
  // stage2 fix: a safe projected CPA alone must NOT clear the give-way duty
  // while the target is still on the bow (here 37.7°, well short of the 90°
  // beam). rule15-cs used to release here — own was still mid-avoidance with
  // the target at -43° relative — producing a premature release and an
  // impossible route return. The target must be past the reference beam.
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
  // Target past the reference beam (95° rel) with a safe projected CPA clears
  // the give-way duty. The CPA-safety semantics are exercised here; the
  // past-beam guard is exercised by BlocksReferenceHeadingReleaseBeforeTargetPastBeam.
  EXPECT_TRUE(give_way_reference_heading_release_safe(
      /*range_m=*/2878.0,
      /*bearing_deg=*/95.0,
      /*target_heading_deg=*/290.0,
      /*target_speed_kn=*/10.61,
      /*own_speed_kn=*/9.88,
      /*reference_heading_deg=*/0.0,
      /*cpa_safe_m=*/926.0));
}

TEST(ColregsReleasePolicy, BlocksOpeningReferenceReleaseBeforeBeamWhenReturnCpaSafe) {
  EXPECT_FALSE(give_way_opening_reference_heading_release_safe(
      /*range_closing=*/false,
      /*range_m=*/1200.0,
      /*bearing_deg=*/285.2,
      /*target_heading_deg=*/290.0,
      /*target_speed_kn=*/10.61,
      /*own_speed_kn=*/9.88,
      /*reference_heading_deg=*/0.0,
      /*cpa_safe_m=*/926.0));
}

TEST(ColregsReleasePolicy, AllowsOpeningReferenceReleasePastBeamWhenReturnCpaSafe) {
  EXPECT_TRUE(give_way_opening_reference_heading_release_safe(
      /*range_closing=*/false,
      /*range_m=*/3719.0,
      /*bearing_deg=*/95.0,
      /*target_heading_deg=*/290.0,
      /*target_speed_kn=*/10.61,
      /*own_speed_kn=*/9.88,
      /*reference_heading_deg=*/0.0,
      /*cpa_safe_m=*/926.0));
}

TEST(ColregsReleasePolicy, BlocksOpeningReferenceReleaseWhileRangeClosing) {
  EXPECT_FALSE(give_way_opening_reference_heading_release_safe(
      /*range_closing=*/true,
      /*range_m=*/3719.0,
      /*bearing_deg=*/285.2,
      /*target_heading_deg=*/290.0,
      /*target_speed_kn=*/10.61,
      /*own_speed_kn=*/9.88,
      /*reference_heading_deg=*/0.0,
      /*cpa_safe_m=*/926.0));
}

TEST(ColregsReleasePolicy, BlocksOpeningReferenceReleaseInsideRangeMargin) {
  EXPECT_FALSE(give_way_opening_reference_heading_release_safe(
      /*range_closing=*/false,
      /*range_m=*/800.0,
      /*bearing_deg=*/285.2,
      /*target_heading_deg=*/290.0,
      /*target_speed_kn=*/10.61,
      /*own_speed_kn=*/9.88,
      /*reference_heading_deg=*/0.0,
      /*cpa_safe_m=*/926.0));
}

TEST(ColregsReleasePolicy, BlocksOpeningReferenceReleaseWhenReturnCpaUnsafe) {
  EXPECT_FALSE(give_way_opening_reference_heading_release_safe(
      /*range_closing=*/false,
      /*range_m=*/3719.0,
      /*bearing_deg=*/45.0,
      /*target_heading_deg=*/290.0,
      /*target_speed_kn=*/10.61,
      /*own_speed_kn=*/9.88,
      /*reference_heading_deg=*/0.0,
      /*cpa_safe_m=*/926.0));
}

TEST(ColregsReleasePolicy, OpeningReferenceReleaseAppliesOnlyToRule15Crossing) {
  EXPECT_TRUE(give_way_opening_reference_release_applies_to_rule(15));
  EXPECT_FALSE(give_way_opening_reference_release_applies_to_rule(13));
}

TEST(ColregsReleasePolicy, AllowsCrossingTargetTrackReleaseAfterPastCpaAsternAndSafe) {
  EXPECT_TRUE(rule15_target_track_release_safe(
      /*range_closing=*/false,
      /*range_m=*/2202.2,
      /*bearing_deg=*/339.6,
      /*target_heading_deg=*/290.0,
      /*cpa_m=*/2207.1,
      /*tcpa_s=*/0.0,
      /*cpa_safe_m=*/900.0));
}

TEST(ColregsReleasePolicy, BlocksCrossingTargetTrackReleaseBeforePastCpa) {
  EXPECT_FALSE(rule15_target_track_release_safe(
      /*range_closing=*/false,
      /*range_m=*/2637.6,
      /*bearing_deg=*/13.2,
      /*target_heading_deg=*/290.0,
      /*cpa_m=*/2201.5,
      /*tcpa_s=*/297.4,
      /*cpa_safe_m=*/900.0));
}

TEST(ColregsReleasePolicy, BlocksCrossingTargetTrackReleaseWhileStillClosing) {
  EXPECT_FALSE(rule15_target_track_release_safe(
      /*range_closing=*/true,
      /*range_m=*/3866.4,
      /*bearing_deg=*/43.8,
      /*target_heading_deg=*/290.0,
      /*cpa_m=*/3878.5,
      /*tcpa_s=*/0.0,
      /*cpa_safe_m=*/900.0));
}

TEST(ColregsReleasePolicy, BlocksCrossingTargetTrackReleaseBeforeOwnIsAsternOfTargetTrack) {
  EXPECT_FALSE(rule15_target_track_release_safe(
      /*range_closing=*/false,
      /*range_m=*/5000.0,
      /*bearing_deg=*/60.0,
      /*target_heading_deg=*/290.0,
      /*cpa_m=*/2200.0,
      /*tcpa_s=*/0.0,
      /*cpa_safe_m=*/900.0));
}

TEST(ColregsReleasePolicy, ProjectionReferenceReleaseAppliesOnlyToRule15Crossing) {
  EXPECT_TRUE(give_way_projection_reference_release_applies_to_rule(15));
  EXPECT_FALSE(give_way_projection_reference_release_applies_to_rule(13));
}

TEST(ColregsReleasePolicy, GiveWayFinalReleaseUsesConfiguredReleaseFloor) {
  EXPECT_DOUBLE_EQ(
      give_way_final_release_cpa_floor_m(
          /*configured_cpa_safe_m=*/1852.0,
          /*configured_cpa_release_m=*/1000.0,
          /*give_way_latched=*/true,
          /*rule13_latched=*/false),
      1000.0);
}

TEST(ColregsReleasePolicy, GiveWayProjectionReleaseCanUseConfiguredReleaseFloor) {
  const double release_floor_m = give_way_final_release_cpa_floor_m(
      /*configured_cpa_safe_m=*/1852.0,
      /*configured_cpa_release_m=*/1000.0,
      /*give_way_latched=*/true,
      /*rule13_latched=*/false);
  EXPECT_TRUE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/1300.0,
      release_floor_m,
      /*current_relative_bearing_abs_deg=*/152.0,
      /*reference_relative_bearing_abs_deg=*/152.0,
      GiveWayProjectionReleaseGate::CURRENT_ABAFT));
}

TEST(ColregsReleasePolicy, NonGiveWayFinalReleaseKeepsSafeFloor) {
  EXPECT_DOUBLE_EQ(
      give_way_final_release_cpa_floor_m(
          /*configured_cpa_safe_m=*/1852.0,
          /*configured_cpa_release_m=*/1000.0,
          /*give_way_latched=*/false,
          /*rule13_latched=*/false),
      1852.0);
}

TEST(ColregsReleasePolicy, BlocksSecondaryGiveWayDutyBeforeTPlanWindow) {
  EXPECT_FALSE(give_way_duty_onset_signal(
      /*raw_own_give_way=*/true,
      /*own_stand_on=*/false,
      /*past_and_clear=*/false,
      /*cpa_projection_past_and_safe=*/false,
      /*tcpa_s=*/1520.0,
      /*cpa_m=*/0.8,
      /*t_plan_s=*/720.0,
      /*cpa_hard_m=*/1852.0,
      /*range_closing=*/true));
}

TEST(ColregsReleasePolicy, AllowsGiveWayDutyInsideTPlanWindow) {
  EXPECT_TRUE(give_way_duty_onset_signal(
      /*raw_own_give_way=*/true,
      /*own_stand_on=*/false,
      /*past_and_clear=*/false,
      /*cpa_projection_past_and_safe=*/false,
      /*tcpa_s=*/719.9,
      /*cpa_m=*/0.8,
      /*t_plan_s=*/720.0,
      /*cpa_hard_m=*/1852.0,
      /*range_closing=*/true));
}

TEST(ColregsReleasePolicy, BlocksSecondaryOnlyGiveWayDutyWithoutPrimaryClassifier) {
  EXPECT_FALSE(give_way_duty_onset_signal(
      /*raw_own_give_way=*/true,
      /*own_stand_on=*/false,
      /*past_and_clear=*/false,
      /*cpa_projection_past_and_safe=*/false,
      /*tcpa_s=*/240.0,
      /*cpa_m=*/0.8,
      /*t_plan_s=*/720.0,
      /*cpa_hard_m=*/1852.0,
      /*range_closing=*/true,
      /*primary_own_give_way=*/false));
}

TEST(ColregsReleasePolicy, BlocksGiveWayDutyWhenNotClosing) {
  EXPECT_FALSE(give_way_duty_onset_signal(
      /*raw_own_give_way=*/true,
      /*own_stand_on=*/false,
      /*past_and_clear=*/false,
      /*cpa_projection_past_and_safe=*/false,
      /*tcpa_s=*/240.0,
      /*cpa_m=*/0.8,
      /*t_plan_s=*/720.0,
      /*cpa_hard_m=*/1852.0,
      /*range_closing=*/false));
}

TEST(ColregsReleasePolicy, PrimaryRuleOnsetDoesNotPreemptLatchedPrimaryRule) {
  EXPECT_TRUE(primary_rule_onset_allowed(
      /*candidate_rule_id=*/15, /*latched_primary_rule_id=*/15));
  EXPECT_TRUE(primary_rule_onset_allowed(
      /*candidate_rule_id=*/14, /*latched_primary_rule_id=*/0));
  EXPECT_FALSE(primary_rule_onset_allowed(
      /*candidate_rule_id=*/14, /*latched_primary_rule_id=*/15));
  EXPECT_FALSE(primary_rule_onset_allowed(
      /*candidate_rule_id=*/15, /*latched_primary_rule_id=*/13));
}

TEST(ColregsReleasePolicy, Rule13FinalReleaseUsesPastClearEmergencyFloor) {
  EXPECT_DOUBLE_EQ(
      give_way_final_release_cpa_floor_m(
          /*configured_cpa_safe_m=*/1852.0,
          /*configured_cpa_release_m=*/1000.0,
          /*give_way_latched=*/true,
          /*rule13_latched=*/true),
      kFinallyPastClearEmergencyCpaM);
}

TEST(ColregsReleasePolicy, Rule13AlongAxisBlocksReleaseWhileOwnStillAbaftTarget) {
  EXPECT_FALSE(rule13_overtaking_along_axis_past_clear(
      /*range_m=*/700.0,
      /*bearing_deg=*/344.0,
      /*target_heading_deg=*/0.0));
}

TEST(ColregsReleasePolicy, Rule13AlongAxisAllowsReleaseAfterOwnPassesTarget) {
  EXPECT_TRUE(rule13_overtaking_along_axis_past_clear(
      /*range_m=*/700.0,
      /*bearing_deg=*/164.0,
      /*target_heading_deg=*/0.0));
}

TEST(ColregsReleasePolicy, Rule13ReleasePastClearRequiresAlongAxisClear) {
  EXPECT_FALSE(rule13_release_past_and_clear(
      /*rule13_release_context=*/true,
      /*bearing_past_and_clear=*/true,
      /*along_axis_past_and_clear=*/false));
  EXPECT_TRUE(rule13_release_past_and_clear(
      /*rule13_release_context=*/true,
      /*bearing_past_and_clear=*/true,
      /*along_axis_past_and_clear=*/true));
  EXPECT_TRUE(rule13_release_past_and_clear(
      /*rule13_release_context=*/false,
      /*bearing_past_and_clear=*/true,
      /*along_axis_past_and_clear=*/false));
}

TEST(ColregsReleasePolicy, BlocksOvertakingProjectionReleaseBeforeRule13PastClear) {
  // Rule 13 must not clear on the generic 90° REFERENCE_CLEAR projection gate.
  // Overtaking release uses finally past-and-clear with the 112.5° Rule 13(b)
  // sector, otherwise M6 releases and reacquires mid-overtake.
  EXPECT_FALSE(give_way_projection_reference_release_applies_to_rule(13) &&
      give_way_projection_release_safe(
          /*cpa_projection_past_and_safe=*/true,
          /*range_m=*/1852.0,
          /*cpa_safe_m=*/926.0,
          /*current_relative_bearing_abs_deg=*/75.0,
          /*reference_relative_bearing_abs_deg=*/95.0,
          GiveWayProjectionReleaseGate::REFERENCE_CLEAR));
}

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

TEST(ColregsReleasePolicy, UsesFsmHeldGiveWayDutyWhenRawGeometryDropsOut) {
  RuleEvaluation held_eval{};
  held_eval.is_active = true;
  held_eval.role = Role::GIVE_WAY;

  EXPECT_TRUE(give_way_duty_from_raw_or_fsm(
      /*raw_give_way_duty=*/false,
      /*fsm_engaged=*/true,
      held_eval));
}

}  // namespace
}  // namespace mass_l3::m6_colregs
