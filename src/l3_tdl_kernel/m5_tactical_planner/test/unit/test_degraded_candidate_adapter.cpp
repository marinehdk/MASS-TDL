#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "l3_msgs/msg/avoidance_plan.hpp"
#include "m5_tactical_planner/committed_route/committed_route.hpp"
#include "m5_tactical_planner/mid_mpc/degraded_candidate_adapter.hpp"

using mass_l3::m5::committed_route::CommittedAvoidanceRoute;
using mass_l3::m5::committed_route::CommittedRouteCandidate;
using mass_l3::m5::committed_route::GeoWP;
using mass_l3::m5::mid_mpc::DegradedCandidatePoint;
using mass_l3::m5::mid_mpc::DegradedCandidateRequest;
using mass_l3::m5::mid_mpc::build_committed_degraded_candidate_plan;
using mass_l3::m5::mid_mpc::build_degraded_candidate_plan;

namespace {

DegradedCandidateRequest base_request()
{
  DegradedCandidateRequest request;
  request.plan_id = "m5-degraded-1";
  request.parent_route_id = "nominal";
  request.behavior_mode = "emergency_avoidance";
  request.confidence = 0.6F;
  request.rationale = "solver_status=1";
  request.nlp_unavailable = true;
  request.points = {
      DegradedCandidatePoint{30.0000, 122.0000, 3.2, "emergency_avoidance"},
      DegradedCandidatePoint{30.0010, 122.0015, 3.2, "emergency_avoidance"},
      DegradedCandidatePoint{30.0020, 122.0030, 3.2, "emergency_avoidance"},
  };
  return request;
}

}  // namespace

TEST(DegradedCandidateAdapter, serializes_only_degraded_corridor_source_labels)
{
  auto request = base_request();
  request.has_return_to_route_point = true;
  request.return_latitude = 30.0020;
  request.return_longitude = 122.0030;

  const auto plan = build_degraded_candidate_plan(request);

  ASSERT_TRUE(plan.has_value());
  ASSERT_EQ(plan->segment_source.size(), plan->latitude.size());
  ASSERT_FALSE(plan->segment_source.empty());
  EXPECT_TRUE(std::all_of(
      plan->segment_source.begin(),
      plan->segment_source.end(),
      [](const std::uint8_t label) {
        return label == l3_msgs::msg::AvoidancePlan::DEGRADED_CORRIDOR;
      }));
  EXPECT_EQ(plan->status, "DEGRADED");
  EXPECT_TRUE(plan->allow_degraded_execution);
}

TEST(DegradedCandidateAdapter, refuses_discontinuous_fallback_when_committed_route_can_continue)
{
  auto request = base_request();
  request.has_return_to_route_point = true;
  request.return_latitude = 30.0020;
  request.return_longitude = 122.0030;
  request.committed_route_can_continue = true;

  const auto plan = build_degraded_candidate_plan(request);

  EXPECT_FALSE(plan.has_value());
}

TEST(DegradedCandidateAdapter, requires_return_to_route_or_explicit_m7_handoff_intent)
{
  auto missing_intent = base_request();
  EXPECT_FALSE(build_degraded_candidate_plan(missing_intent).has_value());

  auto handoff = base_request();
  handoff.safety_concern_event = "m5_degraded_candidate_no_return_route";

  const auto plan = build_degraded_candidate_plan(handoff);

  ASSERT_TRUE(plan.has_value());
  EXPECT_FALSE(plan->has_return_to_route_point);
  EXPECT_NE(plan->rationale.find("safety_concern_event=m5_degraded_candidate_no_return_route"),
            std::string::npos);
  EXPECT_NE(plan->rationale.find("mrm_handoff_intent=m7_only"), std::string::npos);
}


TEST(DegradedCandidateAdapter, rejects_invalid_confidence_values)
{
  auto request = base_request();
  request.has_return_to_route_point = true;
  request.return_latitude = 30.0020;
  request.return_longitude = 122.0030;

  request.confidence = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(build_degraded_candidate_plan(request).has_value());

  request.confidence = -0.01F;
  EXPECT_FALSE(build_degraded_candidate_plan(request).has_value());

  request.confidence = 1.01F;
  EXPECT_FALSE(build_degraded_candidate_plan(request).has_value());
}

TEST(DegradedCandidateAdapter, rejects_invalid_return_to_route_coordinates_when_flagged)
{
  auto request = base_request();
  request.has_return_to_route_point = true;
  request.return_latitude = std::numeric_limits<double>::quiet_NaN();
  request.return_longitude = 122.0030;
  EXPECT_FALSE(build_degraded_candidate_plan(request).has_value());

  request.return_latitude = 30.0020;
  request.return_longitude = std::numeric_limits<double>::infinity();
  EXPECT_FALSE(build_degraded_candidate_plan(request).has_value());
}

TEST(DegradedCandidateAdapter, route_manager_rejection_blocks_committed_degraded_candidate)
{
  CommittedAvoidanceRoute manager;
  CommittedRouteCandidate committed;
  committed.plan_id = "normal-plan";
  committed.valid_until_s = 30.0;
  committed.nlp_ok = true;
  committed.frozen_prefix_count = 2U;
  committed.geometry = std::vector<GeoWP>{
      GeoWP{30.0000, 122.0000, 3.2, "MID_MPC_OPTIMIZED"},
      GeoWP{30.0010, 122.0015, 3.2, "MID_MPC_OPTIMIZED"},
      GeoWP{30.0020, 122.0030, 3.2, "REJOIN_TO_L2"},
  };
  ASSERT_TRUE(manager.try_revise(committed, 0.0));

  auto request = base_request();
  request.has_return_to_route_point = true;
  request.return_latitude = 30.0020;
  request.return_longitude = 122.0030;
  request.points.front().latitude = 30.0100;

  const auto plan = build_committed_degraded_candidate_plan(request, manager, 5.0, 65.0);

  EXPECT_FALSE(plan.has_value());
  EXPECT_EQ(manager.current().plan_id, "normal-plan");
  EXPECT_EQ(manager.current().safety_concern_event, "frozen_prefix_conflict");
}
