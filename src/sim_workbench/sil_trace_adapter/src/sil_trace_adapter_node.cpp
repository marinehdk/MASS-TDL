// sil_trace_adapter_node — L3→SIL decision-trace relay node (Track A A5b).
// Subscribes L3 internal decision/audit topics, publishes the SIL-facing sinks.
// Pure relay: the translator does the ASDR mapping; UIState is a passthrough.
// Replaces the trace portion of sil_topic_bridge.py.
//
// Topics (mirrors the prior bridge wiring):
//   sub  /l3/asdr/record   l3_msgs/ASDRRecord
//   sub  /l3/m8/ui_state   l3_msgs/UIState
//   pub  /sil/asdr_event   sil_msgs/ASDREvent
//   pub  /sil/m8_ui_state  l3_msgs/UIState       (passthrough, same type)
#include <memory>

#include "rclcpp/rclcpp.hpp"

#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/ui_state.hpp"
#include "sil_msgs/msg/asdr_event.hpp"
#include "sil_trace_adapter/translators.hpp"

namespace sil_trace_adapter {

class SilTraceAdapterNode : public rclcpp::Node {
 public:
  SilTraceAdapterNode() : Node("sil_trace_adapter") {
    rclcpp::QoS reliable_qos = rclcpp::QoS(50).reliable().transient_local();
    rclcpp::QoS volatile_qos = rclcpp::QoS(10).reliable();

    sub_asdr_ = create_subscription<l3_msgs::msg::ASDRRecord>(
        "/l3/asdr/record", volatile_qos,
        [this](const l3_msgs::msg::ASDRRecord::SharedPtr msg) {
          pub_asdr_->publish(asdr_record_to_event(*msg));
        });
    sub_ui_ = create_subscription<l3_msgs::msg::UIState>(
        "/l3/m8/ui_state", reliable_qos,
        [this](const l3_msgs::msg::UIState::SharedPtr msg) {
          pub_ui_->publish(*msg);  // passthrough (same type)
        });

    pub_asdr_ = create_publisher<sil_msgs::msg::ASDREvent>(
        "/sil/asdr_event", volatile_qos);
    pub_ui_ = create_publisher<l3_msgs::msg::UIState>(
        "/sil/m8_ui_state", reliable_qos);

    RCLCPP_INFO(get_logger(),
        "sil_trace_adapter ready: sub /l3/asdr/record, /l3/m8/ui_state; "
        "pub /sil/asdr_event, /sil/m8_ui_state");
  }

 private:
  rclcpp::Subscription<l3_msgs::msg::ASDRRecord>::SharedPtr sub_asdr_;
  rclcpp::Subscription<l3_msgs::msg::UIState>::SharedPtr sub_ui_;
  rclcpp::Publisher<sil_msgs::msg::ASDREvent>::SharedPtr pub_asdr_;
  rclcpp::Publisher<l3_msgs::msg::UIState>::SharedPtr pub_ui_;
};

}  // namespace sil_trace_adapter

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<sil_trace_adapter::SilTraceAdapterNode>());
  rclcpp::shutdown();
  return 0;
}
