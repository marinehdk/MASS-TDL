#ifndef MASS_L3_M5_MID_MPC_DEGRADED_CANDIDATE_ADAPTER_HPP_
#define MASS_L3_M5_MID_MPC_DEGRADED_CANDIDATE_ADAPTER_HPP_

#include <optional>
#include <string>
#include <vector>

#include "l3_msgs/msg/avoidance_plan.hpp"
#include "m5_tactical_planner/committed_route/committed_route.hpp"

namespace mass_l3::m5::mid_mpc {

struct DegradedCandidatePoint {
  double latitude{0.0};
  double longitude{0.0};
  double speed_mps{0.0};
  std::string navigation_mode;
};

struct DegradedCandidateRequest {
  std::string plan_id;
  std::string parent_route_id{"nominal"};
  std::string behavior_mode{"emergency_avoidance"};
  std::string rationale;
  std::string safety_concern_event;
  std::vector<DegradedCandidatePoint> points;
  float confidence{0.6F};
  bool nlp_unavailable{false};
  bool committed_route_can_continue{false};
  bool has_return_to_route_point{false};
  double return_latitude{0.0};
  double return_longitude{0.0};
};

[[nodiscard]] std::optional<l3_msgs::msg::AvoidancePlan> build_degraded_candidate_plan(
    const DegradedCandidateRequest& request);

[[nodiscard]] std::optional<l3_msgs::msg::AvoidancePlan> build_committed_degraded_candidate_plan(
    const DegradedCandidateRequest& request,
    mass_l3::m5::committed_route::CommittedAvoidanceRoute& committed_route_manager,
    double now_s,
    double valid_until_s);

}  // namespace mass_l3::m5::mid_mpc

#endif  // MASS_L3_M5_MID_MPC_DEGRADED_CANDIDATE_ADAPTER_HPP_
