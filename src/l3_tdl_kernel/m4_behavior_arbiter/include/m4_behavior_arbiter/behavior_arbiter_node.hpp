#pragma once

#include <memory>
#include <optional>
#include <string>
#include <chrono>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/mode_cmd.hpp"
#include "l3_msgs/msg/mission_goal.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/sat2_data.hpp"
#include "l3_msgs/msg/safety_concern_event.hpp"
#include "l3_msgs/msg/rule_assessment.hpp"
#include "std_msgs/msg/string.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_risk_model/risk_model.hpp"

#include "m4_behavior_arbiter/behavior_activation.hpp"
#include "m4_behavior_arbiter/behavior_dictionary.hpp"
#include "m4_behavior_arbiter/behavior_priority.hpp"
#include "m4_behavior_arbiter/ivp_combine.hpp"
#include "m4_behavior_arbiter/ivp_domain.hpp"
#include "m4_behavior_arbiter/ivp_solver.hpp"
#include "m4_behavior_arbiter/types.hpp"

namespace mass_l3::m4 {

struct IvpContributionInternal {
  float direction_deg;
  float utility_value;
  std::string behavior_id;
};

using ODDStateMsg          = l3_msgs::msg::ODDState;
using WorldStateMsg        = l3_msgs::msg::WorldState;
using ModeCmdMsg           = l3_msgs::msg::ModeCmd;
using MissionGoalMsg       = l3_msgs::msg::MissionGoal;
using COLREGsConstraintMsg = l3_msgs::msg::COLREGsConstraint;
using BehaviorPlanMsg      = l3_msgs::msg::BehaviorPlan;
using ASDRRecordMsg        = l3_msgs::msg::ASDRRecord;
using PlannedRouteMsg      = l3_external_msgs::msg::PlannedRoute;

class BehaviorArbiterNode : public rclcpp::Node {
  friend class BehaviorArbiterTest;
public:
  explicit BehaviorArbiterNode(
      const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

  /// Clear all cross-scenario accumulated state. Called on /sil/scenario_loaded.
  /// Idempotent; safe to call at any time. Clears latches, FSM recovery state,
  /// COLREGs commit anchors, and ranking history so a new scenario starts from
  /// a clean decision slate instead of inheriting the prior run's state.
  void reset_cross_run_state();

private:
  void on_scenario_loaded(const std_msgs::msg::String::SharedPtr msg);
  void on_odd_state(const ODDStateMsg::SharedPtr msg);
  void on_world_state(const WorldStateMsg::SharedPtr msg);
  void on_mode_cmd(const ModeCmdMsg::SharedPtr msg);
  void on_mission_goal(const MissionGoalMsg::SharedPtr msg);
  void on_colregs_constraint(const COLREGsConstraintMsg::SharedPtr msg);
  void on_rule_assessment(const l3_msgs::msg::RuleAssessment::SharedPtr msg);
  void on_planned_route(const PlannedRouteMsg::SharedPtr msg);

  void arbitration_timer_callback();

  // Phase 4: route cross-track error for AVOID→RECOVERY→TRANSIT gating.
  struct RouteTracking {
    double xte_m{0.0};             // signed lateral offset from route line [m]
    double route_heading_deg{0.0}; // bearing of route leg [deg]
    double heading_error_deg{0.0}; // signed own-heading vs route heading [deg]
  };
  std::optional<RouteTracking> current_route_tracking() const;

  ArbitrationInputs build_inputs() const;
  std::vector<IvpContributionInternal> compute_ivp_contributions(
      BehaviorType primary, double h_min, double h_max,
      double confidence) const;
  void publish_asdr_event(const std::string& decision_type,
                          const std::string& decision_json);

  // Cached messages
  ODDStateMsg::SharedPtr          latest_odd_;
  WorldStateMsg::SharedPtr        latest_world_;
  ModeCmdMsg::SharedPtr           latest_mode_;
  MissionGoalMsg::SharedPtr       latest_mission_;
  COLREGsConstraintMsg::SharedPtr latest_colregs_;
  COLREGsConstraintMsg::SharedPtr last_active_colregs_;
  PlannedRouteMsg::SharedPtr      latest_route_;

  bool odd_received_{false};
  bool world_received_{false};
  bool mode_received_{false};
  bool mission_received_{false};
  bool colregs_received_{false};
  bool route_received_{false};

  // Subscriptions
  rclcpp::Subscription<ODDStateMsg>::SharedPtr          sub_odd_;
  rclcpp::Subscription<WorldStateMsg>::SharedPtr        sub_world_;
  rclcpp::Subscription<ModeCmdMsg>::SharedPtr           sub_mode_;
  rclcpp::Subscription<MissionGoalMsg>::SharedPtr       sub_mission_;
  rclcpp::Subscription<COLREGsConstraintMsg>::SharedPtr sub_colregs_;
  rclcpp::Subscription<PlannedRouteMsg>::SharedPtr      sub_route_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr scenario_loaded_sub_;

  // Publishers
  rclcpp::Publisher<BehaviorPlanMsg>::SharedPtr   pub_plan_;
  rclcpp::Publisher<ASDRRecordMsg>::SharedPtr     pub_asdr_;
  rclcpp::Publisher<l3_msgs::msg::SAT2Data>::SharedPtr   pub_sat2_;
  rclcpp::Publisher<l3_msgs::msg::SafetyConcernEvent>::SharedPtr concern_pub_;

  rclcpp::TimerBase::SharedPtr timer_;

  BehaviorDictionary                dictionary_;
  std::unique_ptr<IvPSolver>        solver_;

  // State tracking
  BehaviorType prev_primary_{BehaviorType::MRC_DRIFT};
  uint8_t      prev_odd_zone_{99};
  HealthState  prev_health_{HealthState::Normal};

  bool   m3_active_latch_{false};     // Tracks if M3 has ever been active and valid
  bool   fallback_anchor_set_{false};  // Tracks if the absolute fallback heading is currently set
  double fallback_anchor_hdg_{0.0};    // Stores the latched absolute heading (degrees)
  bool   colregs_anchor_set_{false};   // Tracks the committed COLREG turn reference
  double colregs_anchor_hdg_{0.0};      // Route/own heading at COLREG turn onset (degrees)
  bool   colregs_quartering_gate_{false}; // Conflict-onset latch for stern-quarter edge probes
  bool   colregs_rule15_commit_active_{false}; // Rule15 give-way duty latched for this turn
  std::string colregs_rule15_commit_target_key_; // Target bound to the Rule15 give-way latch
  double colregs_committed_required_dev_deg_{0.0}; // Max bow-crossing give-way turn demand
  int    colregs_inactive_cycles_{0};   // Release dwell for short M6 false gaps
  mass_l3::risk::RankingState risk_ranking_state_;

  // Phase 4 RECOVERY state (architecture §8.3 behavior dictionary addition).
  // AVOID → RECOVERY when COLREGs releases with XTE > corridor_half*0.5.
  // RECOVERY → TRANSIT when XTE restored AND release_dwell satisfied.
  bool   recovery_active_{false};       // currently in RECOVERY behavior
  int    recovery_dwell_cycles_{0};     // cycles XTE has been within gate
  bool   colregs_recovery_armed_{false}; // true after a COLREG turn in current conflict
  bool   colregs_risk_recovery_hold_{false}; // hold RECOVERY after risk-clear release while M6 lags
  // D1.3 v4: cached abaft-beam gate result computed once per arbitration cycle
  // from the fully-extracted colregs_directive (with primary_threat_id). The
  // RECOVERY-entry branch may see a dwell-overridden directive whose
  // primary_threat_id is empty; reading the cached value keeps both release
  // paths (risk_controlled release + RECOVERY entry) on the same geometry.
  bool   last_colregs_target_abaft_beam_{true};
  std::optional<std::uint8_t> active_colregs_rule_id_;
  std::string active_colregs_target_key_;
  std::optional<std::uint8_t> released_colregs_rule_id_;
  std::string released_colregs_target_key_;
  bool released_colregs_rearm_hold_{false};

  rclcpp::Subscription<l3_msgs::msg::RuleAssessment>::SharedPtr sub_rule_assessment_;
  l3_msgs::msg::RuleAssessment::SharedPtr latest_rule_assessment_;
  float colreg_avoidance_weight_{0.6f};

  // Parameters
  int    interval_ms_{250};
  double speed_max_kn_{22.0};
};

}  // namespace mass_l3::m4
