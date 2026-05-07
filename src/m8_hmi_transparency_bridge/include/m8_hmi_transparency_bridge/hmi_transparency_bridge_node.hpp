// include/m8_hmi_transparency_bridge/hmi_transparency_bridge_node.hpp
#ifndef MASS_L3_M8_HMI_TRANSPARENCY_BRIDGE_NODE_HPP_
#define MASS_L3_M8_HMI_TRANSPARENCY_BRIDGE_NODE_HPP_

#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/mission_goal.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/colregs_constraint.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "l3_msgs/msg/ui_state.hpp"
#include "l3_msgs/msg/tor_request.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_external_msgs/msg/override_active_signal.hpp"

#include "m8_hmi_transparency_bridge/sat_aggregator.hpp"
#include "m8_hmi_transparency_bridge/adaptive_sat_trigger.hpp"
#include "m8_hmi_transparency_bridge/tor_protocol.hpp"
#include "m8_hmi_transparency_bridge/ui_state_builder.hpp"
#include "m8_hmi_transparency_bridge/tor_request_generator.hpp"
#include "m8_hmi_transparency_bridge/asdr_logger.hpp"
#include "m8_hmi_transparency_bridge/module_health_monitor.hpp"

namespace mass_l3::m8 {

class HmiTransparencyBridgeNode : public rclcpp::Node {
 public:
  explicit HmiTransparencyBridgeNode(const rclcpp::NodeOptions& options);
  ~HmiTransparencyBridgeNode() override = default;

 private:
  // ---- Subscription callbacks ----
  void on_sat_data(const l3_msgs::msg::SATData::SharedPtr msg);
  void on_odd_state(const l3_msgs::msg::ODDState::SharedPtr msg);
  void on_world_state(const l3_msgs::msg::WorldState::SharedPtr msg);
  void on_mission_goal(const l3_msgs::msg::MissionGoal::SharedPtr msg);
  void on_behavior_plan(const l3_msgs::msg::BehaviorPlan::SharedPtr msg);
  void on_avoidance_plan(const l3_msgs::msg::AvoidancePlan::SharedPtr msg);
  void on_colreg_constraint(const l3_msgs::msg::COLREGsConstraint::SharedPtr msg);
  void on_safety_alert(const l3_msgs::msg::SafetyAlert::SharedPtr msg);
  void on_override_signal(
      const l3_external_msgs::msg::OverrideActiveSignal::SharedPtr msg);

  // ---- Timer callbacks ----
  void on_ui_publish_tick();       // 50 Hz
  void on_tor_tick();              // 2 Hz
  void on_health_check_tick();     // 1 Hz
  void on_asdr_snapshot_tick();    // 2 Hz

  // ---- Helpers ----
  void load_parameters();
  void init_subscriptions();
  void init_publishers();
  void init_timers();
  UiStateBuilder::Scenario infer_scenario() const;
  void publish_tor_request(TorProtocol::Reason reason);
  void emit_asdr_event(const std::string& event_type, const std::string& decision_json);

  // ---- Core modules ----
  std::unique_ptr<SatAggregator> sat_aggregator_;
  std::unique_ptr<AdaptiveSatTrigger> adaptive_trigger_;
  std::unique_ptr<TorProtocol> tor_protocol_;
  std::unique_ptr<UiStateBuilder> ui_builder_;
  std::unique_ptr<ToRRequestGenerator> tor_generator_;
  std::unique_ptr<AsdrLogger> asdr_logger_;
  std::unique_ptr<ModuleHealthMonitor> health_monitor_;

  // ---- State (mutex protected) ----
  mutable std::mutex state_mutex_;
  std::optional<l3_msgs::msg::ODDState> latest_odd_;
  std::optional<l3_msgs::msg::WorldState> latest_world_;
  std::optional<l3_msgs::msg::MissionGoal> latest_mission_;
  std::optional<l3_msgs::msg::BehaviorPlan> latest_behavior_;
  std::optional<l3_msgs::msg::AvoidancePlan> latest_avoidance_;
  std::optional<l3_msgs::msg::COLREGsConstraint> latest_colreg_;
  std::optional<l3_msgs::msg::SafetyAlert> latest_alert_;
  bool override_active_{false};
  bool operator_requested_sat2_{false};

  // ---- ROS2 infra ----
  rclcpp::Subscription<l3_msgs::msg::SATData>::SharedPtr sub_sat_;
  rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr sub_odd_;
  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr sub_world_;
  rclcpp::Subscription<l3_msgs::msg::MissionGoal>::SharedPtr sub_mission_;
  rclcpp::Subscription<l3_msgs::msg::BehaviorPlan>::SharedPtr sub_behavior_;
  rclcpp::Subscription<l3_msgs::msg::AvoidancePlan>::SharedPtr sub_avoid_;
  rclcpp::Subscription<l3_msgs::msg::COLREGsConstraint>::SharedPtr sub_colreg_;
  rclcpp::Subscription<l3_msgs::msg::SafetyAlert>::SharedPtr sub_alert_;
  rclcpp::Subscription<l3_external_msgs::msg::OverrideActiveSignal>::SharedPtr sub_override_;
  rclcpp::Publisher<l3_msgs::msg::UIState>::SharedPtr pub_ui_state_;
  rclcpp::Publisher<l3_msgs::msg::ToRRequest>::SharedPtr pub_tor_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr pub_asdr_;
  rclcpp::TimerBase::SharedPtr timer_ui_;
  rclcpp::TimerBase::SharedPtr timer_tor_;
  rclcpp::TimerBase::SharedPtr timer_health_;
  rclcpp::TimerBase::SharedPtr timer_asdr_snapshot_;

  // ---- Parameters ----
  double tor_deadline_s_{60.0};
  double sat1_min_display_s_{5.0};
  double sat3_priority_high_tdl_s_{30.0};
  double sat2_system_confidence_threshold_{0.6};
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_HMI_TRANSPARENCY_BRIDGE_NODE_HPP_
