#ifndef M7_SAFETY_SUPERVISOR_SAFETY_SUPERVISOR_NODE_HPP_
#define M7_SAFETY_SUPERVISOR_SAFETY_SUPERVISOR_NODE_HPP_

#include <chrono>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "m7_safety_supervisor/iec61508/watchdog_monitor.hpp"
#include "m7_safety_supervisor/iec61508/fault_monitor.hpp"
#include "m7_safety_supervisor/iec61508/diagnostic_coverage.hpp"
#include "m7_safety_supervisor/sotif/assumption_monitor.hpp"
#include "m7_safety_supervisor/sotif/performance_monitor.hpp"
#include "m7_safety_supervisor/sotif/triggering_condition_detector.hpp"
#include "m7_safety_supervisor/checker/veto_handler.hpp"
#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"
#include "m7_safety_supervisor/mrm/mrm_selector.hpp"
#include "m7_safety_supervisor/arbitrator/safety_arbitrator.hpp"
#include "m7_safety_supervisor/arbitrator/alert_generator.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/reactive_override_cmd.hpp"
#include "l3_msgs/msg/colregs_constraint.hpp"
#include "l3_external_msgs/msg/checker_veto_notification.hpp"
#include "l3_external_msgs/msg/reflex_activation_notification.hpp"
#include "l3_external_msgs/msg/override_active_signal.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "std_msgs/msg/header.hpp"

namespace mass_l3::m7 {

class SafetySupervisorNode : public rclcpp::Node {
public:
  explicit SafetySupervisorNode(rclcpp::NodeOptions const& options);

private:
  // Callback groups
  rclcpp::CallbackGroup::SharedPtr cb_group_main_;    // MutuallyExclusive
  rclcpp::CallbackGroup::SharedPtr cb_group_events_;  // Reentrant

  // Subscriptions (9 total)
  rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr sub_odd_;
  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr sub_world_;
  rclcpp::Subscription<l3_msgs::msg::BehaviorPlan>::SharedPtr sub_behavior_;
  rclcpp::Subscription<l3_msgs::msg::AvoidancePlan>::SharedPtr sub_avoidance_;
  rclcpp::Subscription<l3_msgs::msg::ReactiveOverrideCmd>::SharedPtr sub_override_cmd_;
  rclcpp::Subscription<l3_msgs::msg::COLREGsConstraint>::SharedPtr sub_colregs_;
  rclcpp::Subscription<l3_external_msgs::msg::CheckerVetoNotification>::SharedPtr sub_veto_;
  rclcpp::Subscription<l3_external_msgs::msg::ReflexActivationNotification>::SharedPtr sub_reflex_;
  rclcpp::Subscription<l3_external_msgs::msg::OverrideActiveSignal>::SharedPtr sub_override_signal_;

  // Publishers (4 total)
  rclcpp::Publisher<l3_msgs::msg::SafetyAlert>::SharedPtr pub_alert_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr pub_asdr_;
  rclcpp::Publisher<l3_msgs::msg::SATData>::SharedPtr pub_sat_;
  rclcpp::Publisher<std_msgs::msg::Header>::SharedPtr pub_heartbeat_;

  // Timers (4 total)
  rclcpp::TimerBase::SharedPtr timer_main_;           // 4 Hz (250 ms)
  rclcpp::TimerBase::SharedPtr timer_sat_;            // 10 Hz (100 ms)
  rclcpp::TimerBase::SharedPtr timer_asdr_periodic_;  // 2 Hz (500 ms)
  rclcpp::TimerBase::SharedPtr timer_heartbeat_;      // 10 Hz (100 ms)

  // Internal modules (boot-time unique_ptr — allowed by spec)
  std::unique_ptr<iec61508::WatchdogMonitor> watchdog_;
  std::unique_ptr<iec61508::FaultMonitor> fault_monitor_;
  std::unique_ptr<iec61508::DiagnosticCoverage> diag_coverage_;
  std::unique_ptr<sotif::AssumptionMonitor> assumption_monitor_;
  std::unique_ptr<sotif::PerformanceMonitor> performance_monitor_;
  std::unique_ptr<sotif::TriggeringConditionDetector> triggering_detector_;
  std::unique_ptr<checker::VetoHandler> veto_handler_;
  std::unique_ptr<mrm::MrmSelector> mrm_selector_;
  std::unique_ptr<arbitrator::SafetyArbitrator> arbitrator_;

  // Input snapshots (pre-allocated at boot)
  l3_msgs::msg::ODDState last_odd_{};
  l3_msgs::msg::WorldState last_world_{};
  l3_msgs::msg::COLREGsConstraint last_colregs_{};
  l3_msgs::msg::AvoidancePlan last_avoidance_{};
  bool override_active_{false};
  bool reflex_freeze_required_{false};

  // Callbacks — main_loop group
  void on_odd_state(l3_msgs::msg::ODDState::ConstSharedPtr msg);
  void on_world_state(l3_msgs::msg::WorldState::ConstSharedPtr msg);
  void on_behavior_plan(l3_msgs::msg::BehaviorPlan::ConstSharedPtr msg);
  void on_avoidance_plan(l3_msgs::msg::AvoidancePlan::ConstSharedPtr msg);
  void on_colregs_constraint(l3_msgs::msg::COLREGsConstraint::ConstSharedPtr msg);

  // Callbacks — events group
  void on_override_cmd(l3_msgs::msg::ReactiveOverrideCmd::ConstSharedPtr msg);
  void on_checker_veto(l3_external_msgs::msg::CheckerVetoNotification::ConstSharedPtr msg);
  void on_reflex_activation(l3_external_msgs::msg::ReflexActivationNotification::ConstSharedPtr msg);
  void on_override_signal(l3_external_msgs::msg::OverrideActiveSignal::ConstSharedPtr msg);

  // Timer callbacks
  void on_main_loop_tick();
  void on_sat_tick();
  void on_asdr_periodic_tick();
  void on_heartbeat_tick();

  // Helpers
  void setup_subscriptions(rclcpp::QoS const& qos_reliable,
                           rclcpp::QoS const& qos_besteffort);
  void setup_publishers();
  void setup_timers();
  void revert_from_override();
};

}  // namespace mass_l3::m7

#endif  // M7_SAFETY_SUPERVISOR_SAFETY_SUPERVISOR_NODE_HPP_
