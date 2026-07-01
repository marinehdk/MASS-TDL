#include "m5_tactical_planner/mid_mpc/degraded_candidate_adapter.hpp"

#include <cmath>
#include <cstddef>
#include <string>

namespace mass_l3::m5::mid_mpc {
namespace {

bool finite_point(const DegradedCandidatePoint& point)
{
  return std::isfinite(point.latitude) && std::isfinite(point.longitude) &&
         std::isfinite(point.speed_mps) && !point.navigation_mode.empty();
}

}  // namespace

std::optional<l3_msgs::msg::AvoidancePlan> build_degraded_candidate_plan(
    const DegradedCandidateRequest& request)
{
  if (!request.nlp_unavailable || request.committed_route_can_continue || request.points.empty()) {
    return std::nullopt;
  }
  if (!request.has_return_to_route_point && request.safety_concern_event.empty()) {
    return std::nullopt;
  }

  l3_msgs::msg::AvoidancePlan plan;
  plan.schema_version = 114;
  plan.status = "DEGRADED";
  plan.plan_id = request.plan_id;
  plan.parent_route_id = request.parent_route_id;
  plan.behavior_mode = request.behavior_mode;
  plan.command_source = "m5_committed_route";
  plan.allow_degraded_execution = true;
  plan.has_return_to_route_point = request.has_return_to_route_point;
  plan.return_latitude = request.return_latitude;
  plan.return_longitude = request.return_longitude;
  plan.nlp_solver_status = l3_msgs::msg::AvoidancePlan::NLP_NONCONVERGED;
  plan.nlp_kkt_residual = 0.0F;
  plan.nlp_tail_gate_failed = true;
  plan.confidence = request.confidence;
  plan.rationale = "M5 degraded candidate (" + request.rationale + ")";
  if (!request.safety_concern_event.empty()) {
    plan.rationale += "; safety_concern_event=" + request.safety_concern_event;
    plan.rationale += "; mrm_handoff_intent=m7_only";
  }

  plan.latitude.reserve(request.points.size());
  plan.longitude.reserve(request.points.size());
  plan.command_speed_mps.reserve(request.points.size());
  plan.navigation_mode.reserve(request.points.size());
  plan.segment_source.reserve(request.points.size());

  for (const auto& point : request.points) {
    if (!finite_point(point)) {
      return std::nullopt;
    }
    plan.latitude.push_back(point.latitude);
    plan.longitude.push_back(point.longitude);
    plan.command_speed_mps.push_back(point.speed_mps);
    plan.navigation_mode.push_back(point.navigation_mode);
    plan.segment_source.push_back(l3_msgs::msg::AvoidancePlan::DEGRADED_CORRIDOR);
  }

  return plan;
}

}  // namespace mass_l3::m5::mid_mpc
