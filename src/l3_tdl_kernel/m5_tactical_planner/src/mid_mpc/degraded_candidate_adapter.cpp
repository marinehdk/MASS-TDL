#include "m5_tactical_planner/mid_mpc/degraded_candidate_adapter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

#include "m5_tactical_planner/common/units.hpp"

namespace mass_l3::m5::mid_mpc {
namespace {

bool finite_point(const DegradedCandidatePoint& point)
{
  return std::isfinite(point.latitude) && std::isfinite(point.longitude) &&
         std::isfinite(point.speed_mps) && !point.navigation_mode.empty();
}

bool valid_confidence(const float confidence)
{
  return std::isfinite(confidence) && confidence >= 0.0F && confidence <= 1.0F;
}

bool valid_return_to_route_point(const DegradedCandidateRequest& request)
{
  return !request.has_return_to_route_point ||
         (std::isfinite(request.return_latitude) && std::isfinite(request.return_longitude));
}

mass_l3::m5::committed_route::CommittedRouteCandidate committed_candidate_from_degraded_plan(
    const l3_msgs::msg::AvoidancePlan& plan,
    const double valid_until_s)
{
  mass_l3::m5::committed_route::CommittedRouteCandidate candidate;
  candidate.plan_id = plan.plan_id;
  candidate.valid_until_s = valid_until_s;
  candidate.nlp_ok = true;
  candidate.frozen_prefix_count = 0U;
  const std::size_t n = std::min(
      {plan.latitude.size(), plan.longitude.size(), plan.command_speed_mps.size(),
       plan.segment_source.size()});
  candidate.geometry.reserve(n);
  for (std::size_t i = 0U; i < n; ++i) {
    candidate.geometry.push_back(mass_l3::m5::committed_route::GeoWP{
        plan.latitude[i], plan.longitude[i], plan.command_speed_mps[i], "DEGRADED_CORRIDOR"});
  }
  return candidate;
}

}  // namespace

std::optional<l3_msgs::msg::AvoidancePlan> build_degraded_candidate_plan(
    const DegradedCandidateRequest& request)
{
  if (!request.nlp_unavailable || request.committed_route_can_continue || request.points.empty()) {
    return std::nullopt;
  }
  if (!valid_confidence(request.confidence) || !valid_return_to_route_point(request)) {
    return std::nullopt;
  }
  if (!request.has_return_to_route_point && request.safety_concern_event.empty()) {
    return std::nullopt;
  }

  l3_msgs::msg::AvoidancePlan plan;
  plan.schema_version = 116;
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

  // Fix #7 (2026-07-07): populate plan.waypoints (rich struct array) so
  // fcb_simulator::on_avoidance_plan reads the first waypoint for guidance.
  // Without this, msg->waypoints.empty() → simulator ignores the plan → 0° steering.
  plan.waypoints.reserve(request.points.size());
  for (const auto& point : request.points) {
    l3_msgs::msg::AvoidanceWaypoint wp;
    wp.schema_version = 112;
    wp.position.latitude = point.latitude;
    wp.position.longitude = point.longitude;
    wp.position.altitude = 0.0;
    wp.target_speed_kn = point.speed_mps / units::kMsPerKn;
    wp.confidence = request.confidence;
    wp.rationale = "M5 degraded corridor";
    plan.waypoints.push_back(wp);
  }

  return plan;
}

std::optional<l3_msgs::msg::AvoidancePlan> build_committed_degraded_candidate_plan(
    const DegradedCandidateRequest& request,
    mass_l3::m5::committed_route::CommittedAvoidanceRoute& committed_route_manager,
    const double now_s,
    const double valid_until_s)
{
  auto plan = build_degraded_candidate_plan(request);
  if (!plan.has_value()) {
    return std::nullopt;
  }
  if (!committed_route_manager.try_revise(
          committed_candidate_from_degraded_plan(plan.value(), valid_until_s), now_s)) {
    return std::nullopt;
  }
  return plan;
}

}  // namespace mass_l3::m5::mid_mpc
