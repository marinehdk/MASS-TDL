#ifndef MASS_L3_M5_MID_MPC_NODE_HPP_
#define MASS_L3_M5_MID_MPC_NODE_HPP_

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

#include "rclcpp/rclcpp.hpp"

#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_external_msgs/msg/speed_profile.hpp"
#include "ship_interfaces/msg/gnc_execution_odd.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/bc_mpc_health.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/own_ship_state.hpp"
#include "l3_msgs/msg/reactive_override_cmd.hpp"
#include "l3_msgs/msg/safety_concern_event.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_msgs/msg/u_int64.hpp"

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/committed_route/committed_route.hpp"
#include "m5_tactical_planner/committed_route/fallback_manager.hpp"
#include "m5_tactical_planner/avoidance_waypoint_gen.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp"
#ifdef M5_USE_ACADOS
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp"
#endif
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
  [[nodiscard]] MidMpcNlpFormulation::Config resolve_nlp_config_(
      const MidMpcNlpFormulation::Config& cfg);
  [[nodiscard]] MidMpcWaypointGenerator::Config resolve_waypoint_config_(
      const MidMpcWaypointGenerator::Config& cfg,
      double dt_s,
      int32_t n_horizon);

  mass_l3::m5::shared::CapabilityManifest manifest_;
  mass_l3::m5::shared::VesselDynamicsModel vessel_model_;
  NomotoFallbackConfig nomoto_cfg_;
  NomotoFallback nomoto_fallback_;
  MidMpcNlpFormulation        formulation_;
  MidMpcSolver                solver_;
  MidMpcWaypointGenerator     wp_gen_;
#ifdef M5_USE_ACADOS
  std::unique_ptr<MidMpcAcadosFormulation> acados_formulation_;
#endif
  std::optional<MidMpcSolution> last_solution_;
  mass_l3::risk::RankingState risk_ranking_state_;
  mass_l3::m5::committed_route::CommittedAvoidanceRoute committed_route_manager_;
  // P6: FallbackManager tracks FinalDegrade -> MRM state transitions and
  // holds evidence for ASDR audit records (spec SS5.4).
  mass_l3::m5::committed_route::FallbackManager fallback_manager_;

  l3_msgs::msg::WorldState::SharedPtr                        world_state_;
  l3_msgs::msg::BehaviorPlan::SharedPtr                      behavior_plan_;
  l3_msgs::msg::COLREGsConstraint::SharedPtr                 colregs_constraint_;
  l3_external_msgs::msg::PlannedRoute::SharedPtr             planned_route_;
  l3_external_msgs::msg::SpeedProfile::SharedPtr             speed_profile_;

  rclcpp::Publisher<l3_msgs::msg::AvoidancePlan>::SharedPtr  pub_avoidance_plan_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr     pub_asdr_record_;
  rclcpp::Publisher<l3_msgs::msg::SATData>::SharedPtr        pub_sat_data_;
  rclcpp::Publisher<l3_msgs::msg::SAT3Data>::SharedPtr       pub_sat3_data_;
  // P6: SafetyConcernEvent for FINAL_DEGRADE — M7 is the consumer
  rclcpp::Publisher<l3_msgs::msg::SafetyConcernEvent>::SharedPtr pub_safety_concern_;
  // v2.2 §13.1: publish consecutive_failures so BC-MPC (Phase E2) can take over
  // when the NLP solver is stuck. Best-effort QoS — BC-MPC treats a stale/missing
  // value as 0 (no take-over).
  rclcpp::Publisher<std_msgs::msg::UInt64>::SharedPtr         pub_consecutive_failures_;

  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr             sub_world_;
  rclcpp::Subscription<l3_msgs::msg::BehaviorPlan>::SharedPtr           sub_behavior_;
  rclcpp::Subscription<l3_msgs::msg::COLREGsConstraint>::SharedPtr      sub_colregs_;
  rclcpp::Subscription<l3_external_msgs::msg::PlannedRoute>::SharedPtr  sub_route_;
  rclcpp::Subscription<l3_external_msgs::msg::SpeedProfile>::SharedPtr  sub_speed_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr                sub_scenario_loaded_;
  // P6: BC-MPC health metrics — forwarded to committed_route for Condition A
  rclcpp::Subscription<l3_msgs::msg::BcMpcHealth>::SharedPtr            sub_bc_health_;
  // W2: GNC execution-ODD contract (latched). M5 consumes the actual execution
  // limits to generate reachable avoidance geometry (W4) instead of hardcoding.
  rclcpp::Subscription<ship_interfaces::msg::GncExecutionOdd>::SharedPtr sub_gnc_odd_;
  ship_interfaces::msg::GncExecutionOdd latest_gnc_odd_;
  mutable std::mutex gnc_odd_mutex_;

  // P6: cached BC health (mutex-protected, written from health sub callback,
  // read from on_solve_cycle_ main thread)
  l3_msgs::msg::BcMpcHealth last_bc_health_;
  std::mutex bc_health_mutex_;

  // L5: BC-MPC ReactiveOverrideCmd subscription — cached for BcMpcFollow plan
  // construction so the avoidance_plan can carry BC-MPC's heading/speed.
  rclcpp::Subscription<l3_msgs::msg::ReactiveOverrideCmd>::SharedPtr sub_override_cmd_;
  l3_msgs::msg::ReactiveOverrideCmd last_override_cmd_;
  bool has_override_cmd_{false};
  std::mutex override_cmd_mutex_;

  rclcpp::TimerBase::SharedPtr solve_timer_;

  void on_scenario_loaded_(const std_msgs::msg::String::SharedPtr msg);
  // W2: return the live GNC execution ODD, or the hardcoded fallback (matches
  // gnc_avoidance_preflight.hpp defaults) when no ODD msg has arrived yet.
  [[nodiscard]] ship_interfaces::msg::GncExecutionOdd effective_gnc_odd_() const;

  double nominal_speed_kn_{10.0};

  // L0-C: CPA hard floor [m]. Read from ROS param m5.cpa_hard_m at startup;
  // defaults to kCpaSafeFallback_m (1852.0) when param absent or invalid.
  // Calibrate via odd_aware_thresholds.yaml cpa_hard_m (shared with M6/M2).
  double cpa_hard_m_{1852.0};

  void on_solve_cycle_();
  [[nodiscard]] bool has_required_inputs_() const noexcept;
  [[nodiscard]] MidMpcInput assemble_input_();
  void publish_outputs_(const MidMpcSolution& sol,
                        const l3_msgs::msg::AvoidancePlan& plan);
  void publish_avoidance_plan_(const l3_msgs::msg::AvoidancePlan& plan,
                               const std::string& reason);
  // Slice B Keep-Last heartbeat (spec §9.10/§9.12): when the optimized or
  // degraded candidate fails GNC preflight / committed_route gates, this still
  // publishes a plan so the 60s heartbeat does not go silent. Falls back to the
  // committed_route_manager's last active geometry; if none, emits a minimal
  // DEGRADED plan (no waypoints) so GNC sees a stale/keep-last marker rather
  // than losing the route entirely.
  void publish_keep_last_(rclcpp::Time now, const std::string& reason);
  void publish_trajectory_candidates_(const MidMpcInput& input);

  // P6: publish SafetyConcernEvent with CONCERN_BC_FINAL_DEGRADE on FinalDegrade
  void publish_safety_concern_final_degrade_();

  // Phase 1.4 (G-M5-2/3, spec v2.3 §15): audit-trail emitters for the
  // committed-route reject / tail-gate reject paths. These previously only
  // logged via RCLCPP_WARN and stored safety_concern_event in
  // committed_route_manager_'s in-memory state, so the V2.3 phase 3b probe
  // needed container docker logs to recover why each candidate was rejected
  // (optimized_committed_rejected × 790, etc.). Each emitter publishes an
  // ASDRRecord decision_type so the rejection reason lands in the same ASDR
  // audit bus as the rest of M5's decisions.
  void emit_committed_route_rejected_asdr_(
      rclcpp::Time now,
      const std::string& reason,
      const std::string& safety_concern_event,
      const std::string& lifecycle_state_name,
      std::uint32_t consecutive_nlp_failures,
      const std::string& plan_id);
  void emit_tail_gate_rejected_asdr_(
      rclcpp::Time now,
      const std::string& reject_reason,
      const std::string& plan_id,
      double terminal_cpa_m,
      const std::string& target_id);
  // Phase 3.8 (spec v2.3 §14.3 amend): TailBuilder.build rejection (geometry
  // failure such as tail_spacing_invalid) is honest degradation and MUST NOT
  // contaminate candidate.nlp_ok. The NLP solver's convergence verdict is
  // authoritative; mixing TailBuilder geometry failures into nlp_tail_gate_failed
	  // caused 135 spurious optimized_committed_rejected escalations on rule14-ho
	  // (DegradedHold → keep_last empty plan → GNC invalid_avoidance_route × 32).
	  // P4 VR-02: emit_tail_builder_rejected_asdr_ REMOVED (TailBuilder retired).
	  // Phase 1.4 (G-GNC-1, spec v2.3 §15): M5 self-audit when it is about to
  // publish an empty-waypoints avoidance_plan. GNC active_route_manager
  // silently rejects plans with fewer than 2 waypoints (size>=2 hard gate),
  // so an empty plan from M5 BcMpcFollow/KeepLast-empty/TRANSIT branches
  // causes GNC to fall back to route-following without any audit signal on
  // the L3 side. Emitting this ASDR makes the empty-plan hand-off visible
  // without requiring changes to the GNC-side active_route_manager.
  void emit_empty_plan_handoff_asdr_(
      rclcpp::Time now,
      const std::string& reason,
      const std::string& plan_id,
      const std::string& plan_status);

  // P6: emit ASDR record when FinalDegrade is entered so the MRM trigger
  // evidence is recoverable from /l3/asdr/record alone (closes spec §5.4 gap).
  void emit_final_degrade_asdr_(rclcpp::Time now);

  // Publish the committed avoidance route on /l3/m5/avoidance_plan (the only
  // execution-truth topic M5 owns; the legacy /l3/m5/avoidance_waypoints shadow
  // was removed — only sil_trace_writer subscribed and no GNC consumer did).
  //
  // Slice B: every code path through this function MUST reach publish_avoidance_plan_
  // at the end so the 60s heartbeat (spec §9.10) keeps firing even when the
  // optimized/degraded candidate fails GNC preflight — the heartbeat publishes a
  // Keep-Last DEGRADED plan in that case (spec §9.12) instead of silently dropping.
  void publish_committed_route_(rclcpp::Time now,
                                const MidMpcInput& input,
                                double lat0_deg,
                                double lon0_deg,
                                const l3_msgs::msg::AvoidancePlan& selected_plan,
                                const MidMpcSolution& sol);
  // Slice W1 (spec §5.3): build the TailBuilder hold[+rejoin] segment from the
	  // P4 VR-02: append_tail_waypoints_ retired. 1200s NLP covers full lifecycle.
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
  std::optional<std::uint32_t> last_published_route_hash_;
  std::optional<rclcpp::Time> last_avoidance_plan_publish_time_;

  // Fix #8: cache the constraint structure signature so build_symbolic_graph()
  // is only called when the symbolic structure actually changes, not every cycle.
  // Rebuilding the CasADi Function resets IPOPT's L-BFGS Hessian approximation,
  // forcing 500+ iterations to converge from scratch each time.
  std::uint64_t last_constraint_signature_{0};


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
