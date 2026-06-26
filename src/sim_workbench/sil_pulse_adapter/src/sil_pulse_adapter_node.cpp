// sil_pulse_adapter_node — M1-M8 heartbeat aggregation node (Track A A5b).
// Subscribes the 8 L3 module output topics, records last-seen monotonic time
// per module, and publishes a sil_msgs/ModulePulse per module @ 1 Hz with its
// health (GREEN if seen within 10 s, else RED). Replaces the pulse portion of
// sil_topic_bridge.py. The adapter consumes no message payload — it only uses
// topic activity as a liveness signal.
//
// Topics (mirrors the prior bridge wiring):
//   sub /l3/m1/odd_state        l3_msgs/ODDState
//   sub /l3/m2/world_state      l3_msgs/WorldState
//   sub /l3/m3/mission_goal     l3_msgs/MissionGoal
//   sub /l3/m4/behavior_plan    l3_msgs/BehaviorPlan
//   sub /l3/m5/avoidance_plan   l3_msgs/AvoidancePlan
//   sub /l3/m6/colregs_constraint l3_msgs/COLREGsConstraint
//   sub /l3/m7/heartbeat        std_msgs/Header
//   sub /l3/m8/ui_state         l3_msgs/UIState
//   pub /sil/module_pulse       sil_msgs/ModulePulse  (8 msgs/s, 1 per module)
#include <atomic>
#include <chrono>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/header.hpp"

#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/mission_goal.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/ui_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "sil_msgs/msg/module_pulse.hpp"
#include "sil_pulse_adapter/health.hpp"

namespace sil_pulse_adapter {

class SilPulseAdapterNode : public rclcpp::Node {
 public:
  SilPulseAdapterNode() : Node("sil_pulse_adapter") {
    rclcpp::QoS sensor_qos = rclcpp::SensorDataQoS();
    // Each subscription records liveness for its module; payload is unused.
    subs_[kM1] = create_subscription<l3_msgs::msg::ODDState>(
        "/l3/m1/odd_state", sensor_qos,
        [this](l3_msgs::msg::ODDState::SharedPtr) { touch(kM1); });
    subs_[kM2] = create_subscription<l3_msgs::msg::WorldState>(
        "/l3/m2/world_state", sensor_qos,
        [this](l3_msgs::msg::WorldState::SharedPtr) { touch(kM2); });
    subs_[kM3] = create_subscription<l3_msgs::msg::MissionGoal>(
        "/l3/m3/mission_goal", sensor_qos,
        [this](l3_msgs::msg::MissionGoal::SharedPtr) { touch(kM3); });
    subs_[kM4] = create_subscription<l3_msgs::msg::BehaviorPlan>(
        "/l3/m4/behavior_plan", sensor_qos,
        [this](l3_msgs::msg::BehaviorPlan::SharedPtr) { touch(kM4); });
    subs_[kM5] = create_subscription<l3_msgs::msg::AvoidancePlan>(
        "/l3/m5/avoidance_plan", sensor_qos,
        [this](l3_msgs::msg::AvoidancePlan::SharedPtr) { touch(kM5); });
    subs_[kM6] = create_subscription<l3_msgs::msg::COLREGsConstraint>(
        "/l3/m6/colregs_constraint", sensor_qos,
        [this](l3_msgs::msg::COLREGsConstraint::SharedPtr) { touch(kM6); });
    subs_[kM7] = create_subscription<std_msgs::msg::Header>(
        "/l3/m7/heartbeat", sensor_qos,
        [this](std_msgs::msg::Header::SharedPtr) { touch(kM7); });
    subs_[kM8] = create_subscription<l3_msgs::msg::UIState>(
        "/l3/m8/ui_state", sensor_qos,
        [this](l3_msgs::msg::UIState::SharedPtr) { touch(kM8); });

    pub_pulse_ = create_publisher<sil_msgs::msg::ModulePulse>(
        "/sil/module_pulse", sensor_qos);

    // 1 Hz publish timer: emit one ModulePulse per module with its health.
    timer_ = create_wall_timer(std::chrono::seconds(1),
                               [this] { publish_pulses(); });

    start_s_ = wall_now_s();
    RCLCPP_INFO(get_logger(),
        "sil_pulse_adapter ready: sub /l3/m1..m8/* ; pub /sil/module_pulse @1Hz");
  }

 private:
  static double wall_now_s() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  void touch(uint8_t module_id) { last_seen_[module_id].store(wall_now_s()); }

  void publish_pulses() {
    const double now_s = wall_now_s();
    builtin_interfaces::msg::Time stamp;
    {
      const rclcpp::Time t = now();
      stamp.sec = static_cast<int32_t>(t.seconds());
      stamp.nanosec = static_cast<uint32_t>(t.nanoseconds() % 1000000000LL);
    }
    for (uint8_t mid : {kM1, kM2, kM3, kM4, kM5, kM6, kM7, kM8}) {
      const double seen = last_seen_[mid].load();
      // A module that has never been touched has last_seen_ == 0.0; treat that
      // as "never seen" (sentinel) until the first real message arrives. Since
      // the node starts at start_s_ > 0, an untouched module has age > timeout
      // and is naturally RED — but use the explicit sentinel for clarity.
      const double last = (seen == 0.0) ? -1.0 : seen;
      sil_msgs::msg::ModulePulse msg;
      msg.stamp = stamp;
      msg.module_id = mid;
      msg.state = module_health(now_s, last);
      msg.latency_ms = 0u;
      msg.message_drops = 0u;
      pub_pulse_->publish(msg);
    }
  }

  // Store the typed subscriptions (base type erased) so they stay alive.
  rclcpp::SubscriptionBase::SharedPtr subs_[9];  // index 1..8
  rclcpp::Publisher<sil_msgs::msg::ModulePulse>::SharedPtr pub_pulse_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::atomic<double> last_seen_[9] = {};
  double start_s_{0.0};
};

}  // namespace sil_pulse_adapter

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<sil_pulse_adapter::SilPulseAdapterNode>());
  rclcpp::shutdown();
  return 0;
}
