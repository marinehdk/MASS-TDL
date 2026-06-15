#pragma once

#include <memory>
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

class BehaviorArbiterNode : public rclcpp::Node {
  friend class BehaviorArbiterTest;
public:
  explicit BehaviorArbiterNode(
      const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

private:
  void on_odd_state(const ODDStateMsg::SharedPtr msg);
  void on_world_state(const WorldStateMsg::SharedPtr msg);
  void on_mode_cmd(const ModeCmdMsg::SharedPtr msg);
  void on_mission_goal(const MissionGoalMsg::SharedPtr msg);
  void on_colregs_constraint(const COLREGsConstraintMsg::SharedPtr msg);
  void on_rule_assessment(const l3_msgs::msg::RuleAssessment::SharedPtr msg);

  void arbitration_timer_callback();

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

  bool odd_received_{false};
  bool world_received_{false};
  bool mode_received_{false};
  bool mission_received_{false};
  bool colregs_received_{false};

  // Subscriptions
  rclcpp::Subscription<ODDStateMsg>::SharedPtr          sub_odd_;
  rclcpp::Subscription<WorldStateMsg>::SharedPtr        sub_world_;
  rclcpp::Subscription<ModeCmdMsg>::SharedPtr           sub_mode_;
  rclcpp::Subscription<MissionGoalMsg>::SharedPtr       sub_mission_;
  rclcpp::Subscription<COLREGsConstraintMsg>::SharedPtr sub_colregs_;

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
  double colregs_committed_required_dev_deg_{0.0}; // Max bow-crossing give-way turn demand
  int    colregs_inactive_cycles_{0};   // Release dwell for short M6 false gaps
  mass_l3::risk::RankingState risk_ranking_state_;

  rclcpp::Subscription<l3_msgs::msg::RuleAssessment>::SharedPtr sub_rule_assessment_;
  l3_msgs::msg::RuleAssessment::SharedPtr latest_rule_assessment_;
  float colreg_avoidance_weight_{0.6f};

  // Parameters
  int    interval_ms_{250};
  double speed_max_kn_{22.0};
};

}  // namespace mass_l3::m4
