// sil_fusion_adapter_node — SIL→L3 fusion relay node (Track A A5b).
// Subscribes SIL sensor-side topics, publishes the L3 fusion-side external
// topics. Pure relay: the translators do all field-mapping; this node holds no
// behavior state. Replaces the fusion portion of sil_topic_bridge.py.
//
// Topics (mirrors the prior bridge wiring):
//   sub  /sil/target_vessel_state  sil_msgs/TargetVesselState       (sensor QoS)
//   sub  /sil/environment          sil_msgs/EnvironmentState        (sensor QoS)
//   pub  /fusion/tracked_targets   l3_external_msgs/TrackedTargetArray
//   pub  /fusion/environment_state l3_external_msgs/EnvironmentState
#include <memory>

#include "rclcpp/rclcpp.hpp"

#include "l3_external_msgs/msg/environment_state.hpp"
#include "l3_external_msgs/msg/tracked_target_array.hpp"
#include "sil_fusion_adapter/translators.hpp"
#include "sil_msgs/msg/environment_state.hpp"
#include "sil_msgs/msg/target_vessel_state.hpp"

namespace sil_fusion_adapter {

class SilFusionAdapterNode : public rclcpp::Node {
 public:
  SilFusionAdapterNode() : Node("sil_fusion_adapter") {
    // Sensor-side subscriptions: BEST_EFFORT / VOLATILE (matches SIL publishers).
    rclcpp::QoS sensor_qos = rclcpp::SensorDataQoS();
    sub_target_ = create_subscription<sil_msgs::msg::TargetVesselState>(
        "/sil/target_vessel_state", sensor_qos,
        [this](const sil_msgs::msg::TargetVesselState::SharedPtr msg) {
          pub_targets_->publish(target_vessel_to_tracked_array(*msg));
        });
    sub_env_ = create_subscription<sil_msgs::msg::EnvironmentState>(
        "/sil/environment", sensor_qos,
        [this](const sil_msgs::msg::EnvironmentState::SharedPtr msg) {
          pub_env_->publish(environment_sil_to_l3(*msg));
        });

    // L3-side publishers: RELIABLE / VOLATILE (l3_external fusion inputs).
    rclcpp::QoS reliable_qos = rclcpp::QoS(10).reliable();
    pub_targets_ = create_publisher<l3_external_msgs::msg::TrackedTargetArray>(
        "/fusion/tracked_targets", reliable_qos);
    pub_env_ = create_publisher<l3_external_msgs::msg::EnvironmentState>(
        "/fusion/environment_state", reliable_qos);

    RCLCPP_INFO(get_logger(),
        "sil_fusion_adapter ready: sub /sil/target_vessel_state, /sil/environment; "
        "pub /fusion/tracked_targets, /fusion/environment_state");
  }

 private:
  rclcpp::Subscription<sil_msgs::msg::TargetVesselState>::SharedPtr sub_target_;
  rclcpp::Subscription<sil_msgs::msg::EnvironmentState>::SharedPtr sub_env_;
  rclcpp::Publisher<l3_external_msgs::msg::TrackedTargetArray>::SharedPtr pub_targets_;
  rclcpp::Publisher<l3_external_msgs::msg::EnvironmentState>::SharedPtr pub_env_;
};

}  // namespace sil_fusion_adapter

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<sil_fusion_adapter::SilFusionAdapterNode>());
  rclcpp::shutdown();
  return 0;
}
