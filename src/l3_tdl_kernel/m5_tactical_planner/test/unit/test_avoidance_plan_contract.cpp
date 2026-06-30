#include <gtest/gtest.h>

#include <cstdint>

#include "l3_msgs/msg/avoidance_plan.hpp"
#include "m5_tactical_planner/avoidance_route_hash.hpp"

namespace {

TEST(AvoidancePlanContract, exposes_committed_route_schema_v114_fields) {
  l3_msgs::msg::AvoidancePlan msg;

  msg.segment_source = {
      l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED,
      l3_msgs::msg::AvoidancePlan::MID_MPC_TERMINAL_HOLD,
      l3_msgs::msg::AvoidancePlan::REJOIN_TO_L2,
      l3_msgs::msg::AvoidancePlan::L2_NOMINAL_SUFFIX,
      l3_msgs::msg::AvoidancePlan::DEGRADED_CORRIDOR,
  };
  msg.latitude = {30.0, 30.001, 30.002, 30.003, 30.004};
  msg.longitude = {122.0, 122.001, 122.002, 122.003, 122.004};
  msg.command_speed_mps = {5.0, 5.0, 4.5, 4.5, 3.2};
  msg.navigation_mode = {
      "colregs_avoidance",
      "colregs_avoidance",
      "return_to_route",
      "transit",
      "emergency_avoidance",
  };
  msg.route_hash = 0xdeadbeefu;
  msg.nlp_solver_status = l3_msgs::msg::AvoidancePlan::NLP_CONVERGED;
  msg.nlp_kkt_residual = 0.01F;
  msg.nlp_tail_gate_failed = false;

  EXPECT_EQ(msg.schema_version, 114u);
  EXPECT_EQ(msg.segment_source.size(), msg.latitude.size());
  EXPECT_EQ(msg.latitude.size(), msg.longitude.size());
  EXPECT_EQ(msg.command_speed_mps.size(), msg.latitude.size());
  EXPECT_EQ(msg.navigation_mode.size(), msg.latitude.size());
  EXPECT_EQ(msg.route_hash, 0xdeadbeefu);
  EXPECT_EQ(msg.nlp_solver_status, l3_msgs::msg::AvoidancePlan::NLP_CONVERGED);
  EXPECT_FALSE(msg.nlp_tail_gate_failed);
}


TEST(AvoidancePlanContract, route_hash_ignores_volatile_plan_id_for_identical_route_content) {
  l3_msgs::msg::AvoidancePlan first;
  first.plan_id = "m5-midmpc-111";
  first.parent_route_id = "nominal";
  first.behavior_mode = "colregs_avoidance";
  first.command_source = "m5_committed_route";
  first.latitude = {30.0, 30.001, 30.002};
  first.longitude = {122.0, 122.001, 122.002};
  first.command_speed_mps = {5.0, 5.0, 4.5};
  first.navigation_mode = {"colregs_avoidance", "colregs_avoidance", "transit"};
  first.segment_source = {
      l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED,
      l3_msgs::msg::AvoidancePlan::MID_MPC_TERMINAL_HOLD,
      l3_msgs::msg::AvoidancePlan::L2_NOMINAL_SUFFIX,
  };
  first.allow_degraded_execution = false;
  first.has_return_to_route_point = false;
  first.nlp_solver_status = l3_msgs::msg::AvoidancePlan::NLP_CONVERGED;
  first.nlp_tail_gate_failed = false;

  auto second = first;
  second.plan_id = "m5-midmpc-222";

  EXPECT_EQ(
      mass_l3::m5::avoidance_route_hash(first),
      mass_l3::m5::avoidance_route_hash(second));
}

TEST(AvoidancePlanContract, defaults_to_non_stale_zero_timestamp) {
  l3_msgs::msg::AvoidancePlan msg;

  EXPECT_EQ(msg.stale_committed_at.sec, 0);
  EXPECT_EQ(msg.stale_committed_at.nanosec, 0u);
}

}  // namespace
