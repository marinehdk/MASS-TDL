#include <gtest/gtest.h>
#include "m4_behavior_arbiter/behavior_activation.hpp"

namespace mass_l3::m4 {
namespace {

ArbitrationInputs make_normal_inputs() {
  ArbitrationInputs in;
  in.odd_zone = 0;
  in.odd_received = true;
  in.mode_received = true;
  in.mode_mrc_triggered = false;
  in.world_received = true;
  in.world_visibility_nm = 999.0;
  in.world_in_vts_zone = false;
  in.own_speed_kn = 10.0;
  in.mission_received = true;
  in.mission_heading_desired_deg = 45.0;
  in.colregs_received = true;
  in.colregs_conflict_detected = false;
  in.age_odd_ms = 500;
  in.age_world_ms = 200;
  in.age_mission_ms = 1000;
  in.age_colregs_ms = 300;
  return in;
}

TEST(BehaviorActivationTest, TransitActiveInOddAWithMission) {
  auto in = make_normal_inputs();
  EXPECT_TRUE(BehaviorActivationCondition::is_transit_applicable(in));
}

TEST(BehaviorActivationTest, TransitInactiveWhenNoMission) {
  auto in = make_normal_inputs();
  in.mission_received = false;
  EXPECT_FALSE(BehaviorActivationCondition::is_transit_applicable(in));
}

TEST(BehaviorActivationTest, TransitInactiveInOddD) {
  auto in = make_normal_inputs();
  in.odd_zone = 3;
  EXPECT_FALSE(BehaviorActivationCondition::is_transit_applicable(in));
}

TEST(BehaviorActivationTest, ColregAvoidActiveWhenConflictDetected) {
  auto in = make_normal_inputs();
  in.colregs_conflict_detected = true;
  EXPECT_TRUE(BehaviorActivationCondition::is_colreg_avoid_applicable(in));
}

TEST(BehaviorActivationTest, ColregAvoidInactiveWhenNoConflict) {
  auto in = make_normal_inputs();
  EXPECT_FALSE(BehaviorActivationCondition::is_colreg_avoid_applicable(in));
}

TEST(BehaviorActivationTest, ColregAvoidInactiveWhenColregsStale) {
  auto in = make_normal_inputs();
  in.colregs_conflict_detected = true;
  in.age_colregs_ms = 5000;
  EXPECT_FALSE(BehaviorActivationCondition::is_colreg_avoid_applicable(in));
}

TEST(BehaviorActivationTest, RestrictedVisActiveInOddDWithLowVisibility) {
  auto in = make_normal_inputs();
  in.odd_zone = 3;
  in.world_visibility_nm = 0.5;
  EXPECT_TRUE(BehaviorActivationCondition::is_restricted_vis_applicable(in));
}

TEST(BehaviorActivationTest, RestrictedVisInactiveWhenVisibilityGood) {
  auto in = make_normal_inputs();
  in.odd_zone = 3;
  in.world_visibility_nm = 5.0;
  EXPECT_FALSE(BehaviorActivationCondition::is_restricted_vis_applicable(in));
}

TEST(BehaviorActivationTest, RestrictedVisInactiveWhenStopped) {
  auto in = make_normal_inputs();
  in.odd_zone = 3;
  in.world_visibility_nm = 0.5;
  in.own_speed_kn = 0.0;
  EXPECT_FALSE(BehaviorActivationCondition::is_restricted_vis_applicable(in));
}

TEST(BehaviorActivationTest, ChannelFollowActiveInOddBWithVts) {
  auto in = make_normal_inputs();
  in.odd_zone = 1;
  in.world_in_vts_zone = true;
  EXPECT_TRUE(BehaviorActivationCondition::is_channel_follow_applicable(in));
}

TEST(BehaviorActivationTest, ChannelFollowInactiveOutsideOddB) {
  auto in = make_normal_inputs();
  in.odd_zone = 0;
  in.world_in_vts_zone = true;
  EXPECT_FALSE(BehaviorActivationCondition::is_channel_follow_applicable(in));
}

TEST(BehaviorActivationTest, MrcDriftActiveWhenModeTriggered) {
  auto in = make_normal_inputs();
  in.mode_mrc_triggered = true;
  EXPECT_TRUE(BehaviorActivationCondition::is_mrc_drift_applicable(in));
}

TEST(BehaviorActivationTest, MrcDriftActiveWhenAllInputsStale) {
  auto in = make_normal_inputs();
  in.age_odd_ms = 6000;
  in.age_world_ms = 6000;
  in.age_mission_ms = 6000;
  in.age_colregs_ms = 6000;
  EXPECT_TRUE(BehaviorActivationCondition::is_mrc_drift_applicable(in));
}

TEST(BehaviorActivationTest, MrcDriftExclusiveInActiveSet) {
  auto in = make_normal_inputs();
  in.colregs_conflict_detected = true;
  in.mode_mrc_triggered = true;
  auto active = BehaviorActivationCondition::compute_active_set(in, BehaviorDictionary{});
  ASSERT_EQ(active.size(), 1u);
  EXPECT_EQ(active[0], BehaviorType::MRC_DRIFT);
}

TEST(BehaviorActivationTest, NormalHealthWhenAllInputsFresh) {
  auto in = make_normal_inputs();
  EXPECT_EQ(BehaviorActivationCondition::compute_health_state(in), HealthState::Normal);
}

TEST(BehaviorActivationTest, DegradedHealthWhenOneInputStale) {
  auto in = make_normal_inputs();
  in.age_world_ms = 3000;
  EXPECT_EQ(BehaviorActivationCondition::compute_health_state(in), HealthState::Degraded);
}

TEST(BehaviorActivationTest, CriticalHealthWhenMostInputsStale) {
  auto in = make_normal_inputs();
  in.age_odd_ms = 6000;
  in.age_world_ms = 6000;
  in.age_mission_ms = 6000;
  EXPECT_EQ(BehaviorActivationCondition::compute_health_state(in), HealthState::Critical);
}

}  // namespace
}  // namespace mass_l3::m4
