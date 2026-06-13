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

TEST(ColregsReleasePolicy, AllowsCrossingProjectionReleaseAfterReferenceClear) {
  EXPECT_TRUE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/3883.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/125.0,
      /*reference_relative_bearing_abs_deg=*/40.0,
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

TEST(ColregsReleasePolicy, AllowsReferenceHeadingReleaseWhenReturnCpaSafe) {
  EXPECT_TRUE(give_way_reference_heading_release_safe(
      /*range_m=*/2878.0,
      /*bearing_deg=*/37.7,
      /*target_heading_deg=*/290.0,
      /*target_speed_kn=*/10.61,
      /*own_speed_kn=*/9.88,
      /*reference_heading_deg=*/0.0,
      /*cpa_safe_m=*/926.0));
}

TEST(ColregsReleasePolicy, AllowsOvertakingProjectionReleaseAfterReferenceClear) {
  EXPECT_TRUE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/1852.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/75.0,
      /*reference_relative_bearing_abs_deg=*/48.0,
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

}  // namespace
}  // namespace mass_l3::m6_colregs
