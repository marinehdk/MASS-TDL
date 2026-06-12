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

TEST(ColregsReleasePolicy, AllowsOvertakingProjectionReleaseAfterReferenceClear) {
  EXPECT_TRUE(give_way_projection_release_safe(
      /*cpa_projection_past_and_safe=*/true,
      /*range_m=*/1852.0,
      /*cpa_safe_m=*/926.0,
      /*current_relative_bearing_abs_deg=*/75.0,
      /*reference_relative_bearing_abs_deg=*/48.0,
      GiveWayProjectionReleaseGate::REFERENCE_CLEAR));
}

}  // namespace
}  // namespace mass_l3::m6_colregs
