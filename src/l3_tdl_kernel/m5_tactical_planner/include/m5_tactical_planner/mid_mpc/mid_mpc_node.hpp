#ifndef MASS_L3_M5_MID_MPC_NODE_HPP_
#define MASS_L3_M5_MID_MPC_NODE_HPP_

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"

#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_external_msgs/msg/speed_profile.hpp"
#include "l3_external_msgs/msg/avoidance_waypoints.hpp"
#include "ship_interfaces/msg/gnc_execution_odd.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/own_ship_state.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "std_msgs/msg/string.hpp"

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/avoidance_waypoint_gen.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_waypoint_generator.hpp"
#include "m5_tactical_planner/shared/capability_manifest.hpp"
#include "m5_tactical_planner/shared/vessel_dynamics_model.hpp"

#include "l3_msgs/msg/sat3_data.hpp"
#include "l3_msgs/msg/trajectory_candidate.hpp"
#include "l3_risk_model/risk_model.hpp"
#include "m5_tactical_planner/mid_mpc/nomoto_fallback.hpp"

namespace mass_l3::m5::mid_mpc {

class MidMpcNode : public rclcpp::Node {
 public:
  struct Config {
    MidMpcNlpFormulation::Config nlp;
    MidMpcSolver::IpoptOptions   ipopt;
    MidMpcWaypointGenerator::Config waypoint;
    // [TBD-HAZID] Default FCB home port latitude [deg WGS84].
    // Replace with dynamic own-ship state in Phase E2.
    double own_ship_lat_deg{30.0};
    // [TBD-HAZID] Default FCB home port longitude [deg WGS84].
    double own_ship_lon_deg{122.0};
  };

  explicit MidMpcNode(const Config& cfg);

  /// Clear cross-scenario MPC warm state on new scenario. Idempotent.
  /// Drops last_solution_ (warm-start) and ranking history so the next
  /// scenario cold-starts the solver.
  void reset_cross_run_state();

 private:
  mass_l3::m5::shared::CapabilityManifest manifest_;
  mass_l3::m5::shared::VesselDynamicsModel vessel_model_;
  NomotoFallbackConfig nomoto_cfg_;
  NomotoFallback nomoto_fallback_;
  MidMpcNlpFormulation        formulation_;
  MidMpcSolver                solver_;
  MidMpcWaypointGenerator     wp_gen_;
  std::optional<MidMpcSolution> last_solution_;
  mass_l3::risk::RankingState risk_ranking_state_;

  l3_msgs::msg::WorldState::SharedPtr                        world_state_;
  l3_msgs::msg::BehaviorPlan::SharedPtr                      behavior_plan_;
  l3_msgs::msg::COLREGsConstraint::SharedPtr                 colregs_constraint_;
  l3_external_msgs::msg::PlannedRoute::SharedPtr             planned_route_;
  l3_external_msgs::msg::SpeedProfile::SharedPtr             speed_profile_;

  rclcpp::Publisher<l3_msgs::msg::AvoidancePlan>::SharedPtr  pub_avoidance_plan_;
  // Track A A3: L3-owned waypoint plan consumed by the GNC bridge (A4).
  rclcpp::Publisher<l3_external_msgs::msg::AvoidanceWaypoints>::SharedPtr pub_avoidance_waypoints_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr     pub_asdr_record_;
  rclcpp::Publisher<l3_msgs::msg::SATData>::SharedPtr        pub_sat_data_;
  rclcpp::Publisher<l3_msgs::msg::SAT3Data>::SharedPtr       pub_sat3_data_;

  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr             sub_world_;
  rclcpp::Subscription<l3_msgs::msg::BehaviorPlan>::SharedPtr           sub_behavior_;
  rclcpp::Subscription<l3_msgs::msg::COLREGsConstraint>::SharedPtr      sub_colregs_;
  rclcpp::Subscription<l3_external_msgs::msg::PlannedRoute>::SharedPtr  sub_route_;
  rclcpp::Subscription<l3_external_msgs::msg::SpeedProfile>::SharedPtr  sub_speed_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr                sub_scenario_loaded_;
  // W2: GNC execution-ODD contract (latched). M5 consumes the actual execution
  // limits to generate reachable avoidance geometry (W4) instead of hardcoding.
  rclcpp::Subscription<ship_interfaces::msg::GncExecutionOdd>::SharedPtr sub_gnc_odd_;
  ship_interfaces::msg::GncExecutionOdd latest_gnc_odd_;
  mutable std::mutex gnc_odd_mutex_;

  rclcpp::TimerBase::SharedPtr solve_timer_;

  void on_scenario_loaded_(const std_msgs::msg::String::SharedPtr msg);
  // W2: return the live GNC execution ODD, or the hardcoded fallback (matches
  // gnc_avoidance_preflight.hpp defaults) when no ODD msg has arrived yet.
  [[nodiscard]] ship_interfaces::msg::GncExecutionOdd effective_gnc_odd_() const;

  double nominal_speed_kn_{10.0};

  void on_solve_cycle_();
  [[nodiscard]] bool has_required_inputs_() const noexcept;
  [[nodiscard]] MidMpcInput assemble_input_();
  void publish_outputs_(const MidMpcSolution& sol,
                        const l3_msgs::msg::AvoidancePlan& plan);
  void publish_trajectory_candidates_(const MidMpcInput& input);

  // Track A A3: emit the L3-owned waypoint plan on /l3/m5/avoidance_waypoints
  // for the GNC bridge to translate. Called from publish_outputs_ with the same
  // avoidance/intent context. When behavior is not avoidance (transit/recovery,
  // i.e. M6 conflict clear), return_to_route is emitted for a short delivery
  // window so the GNC route-update guard cannot drop the only release message.
  void publish_avoidance_waypoints_(rclcpp::Time now,
                                    const MidMpcInput& input,
                                    double lat0_deg,
                                    double lon0_deg);
  bool last_emitted_conflict_active_{false};
  std::optional<rclcpp::Time> return_to_route_emit_until_;
  struct AvoidanceCorridorAnchor {
    double lat_deg{0.0};
    double lon_deg{0.0};
    double heading_min_deg{0.0};
    double heading_max_deg{0.0};
    double command_speed_mps{0.0};
    double route_bearing_rad{0.0};
    double route_xte_sign{1.0};
    mass_l3::m5::ColregsPreferredDirection direction{mass_l3::m5::ColregsPreferredDirection::Hold};
    bool colregs_overtake_corridor{false};
    std::string plan_id;
  };
  struct ReturnRouteAnchor {
    std::vector<mass_l3::m5::WaypointLatLon> waypoints;
    double command_speed_mps{0.0};
    std::string navigation_mode{"emergency_avoidance"};
    std::string plan_id;
  };
  std::optional<AvoidanceCorridorAnchor> avoidance_corridor_anchor_;
  std::optional<ReturnRouteAnchor> return_route_anchor_;

  // Geometric starboard fallback: generates arc waypoints from vessel kinematics
  // when the NLP solver fails or M4 signals a geometric starboard requirement.
  [[nodiscard]] l3_msgs::msg::AvoidancePlan build_geometric_fallback_plan_(
      const MidMpcInput& input,
      double lat0_deg,
      double lon0_deg,
      const std::string& reason);

  // Phase 4 RECOVERY: gradual return-to-route trajectory (architecture §7.2).
  // N waypoints whose XTE decays linearly toward the route over the horizon.
  [[nodiscard]] l3_msgs::msg::AvoidancePlan build_recovery_plan_(
      const MidMpcInput& input,
      double lat0_deg,
      double lon0_deg);
};

}  // namespace mass_l3::m5::mid_mpc

#endif  // MASS_L3_M5_MID_MPC_NODE_HPP_
