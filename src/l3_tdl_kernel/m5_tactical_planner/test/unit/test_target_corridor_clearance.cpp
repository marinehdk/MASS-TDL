#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "m5_tactical_planner/target_corridor_clearance.hpp"

using mass_l3::m5::TargetTrackPoint;
using mass_l3::m5::sample_target_track;
using mass_l3::m5::closest_target_to_segment_m;
using mass_l3::m5::TargetClearanceVerdict;
using mass_l3::m5::evaluate_target_corridor_clearance;

TEST(SampleTargetTrack, PropagatesConstantCogSog) {
  TargetTrackPoint t0{0.0, 0.0, 90.0 * M_PI / 180.0, 10.0};
  const auto track = sample_target_track(t0, 30.0, 60.0);
  ASSERT_EQ(track.size(), 3u);
  EXPECT_NEAR(track[0].x_m, 0.0, 1e-6);
  EXPECT_NEAR(track[0].y_m, 0.0, 1e-6);
  EXPECT_NEAR(track[1].x_m, 0.0, 1e-6);
  EXPECT_NEAR(track[1].y_m, 300.0, 1e-3);
  EXPECT_NEAR(track[2].y_m, 600.0, 1e-3);
}

TEST(ClosestTargetToSegment, PointToSegmentPerpendicular) {
  const double d = closest_target_to_segment_m(500.0, 100.0, 0.0, 0.0, 1000.0, 0.0);
  EXPECT_NEAR(d, 100.0, 1e-3);
}

TEST(ClosestTargetToSegment, PointBeforeSegment) {
  const double d = closest_target_to_segment_m(-200.0, 50.0, 0.0, 0.0, 1000.0, 0.0);
  EXPECT_NEAR(d, std::hypot(200.0, 50.0), 1e-3);
}

TEST(EvaluateTargetCorridorClearance, PassesWhenTargetFarFromCorridor) {
  TargetTrackPoint t0{0.0, 870.0, 0.0, 6.0};
  const TargetClearanceVerdict v = evaluate_target_corridor_clearance(
      t0, 0.0, 0.0, 2000.0, 270.0, 200.0, 600.0, 30.0);
  EXPECT_TRUE(v.clear);
  EXPECT_GT(v.min_separation_m, 200.0);
}

TEST(EvaluateTargetCorridorClearance, FailsWhenTargetCrossesCorridor) {
  TargetTrackPoint t0{0.0, 100.0, 45.0 * M_PI / 180.0, 8.0};
  const TargetClearanceVerdict v = evaluate_target_corridor_clearance(
      t0, 0.0, 0.0, 3000.0, 270.0, 200.0, 600.0, 30.0);
  EXPECT_FALSE(v.clear);
  EXPECT_LT(v.min_separation_m, 200.0);
}
