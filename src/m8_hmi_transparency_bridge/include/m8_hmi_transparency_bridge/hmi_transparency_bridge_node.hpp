#ifndef MASS_L3_M8_HMI_TRANSPARENCY_BRIDGE_NODE_HPP_
#define MASS_L3_M8_HMI_TRANSPARENCY_BRIDGE_NODE_HPP_

#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "l3_msgs/msg/sat_data.hpp"

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
  explicit HmiTransparencyBridgeNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions{});
  ~HmiTransparencyBridgeNode() override = default;
  HmiTransparencyBridgeNode(const HmiTransparencyBridgeNode&) = delete;
  HmiTransparencyBridgeNode& operator=(const HmiTransparencyBridgeNode&) = delete;

 private:
  void on_sat_data(const l3_msgs::msg::SATData::SharedPtr msg);

  std::unique_ptr<SatAggregator> aggregator_;
  std::unique_ptr<AdaptiveSatTrigger> sat_trigger_;
  std::unique_ptr<TorProtocol> tor_protocol_;
  std::unique_ptr<UiStateBuilder> ui_builder_;
  std::unique_ptr<TorRequestGenerator> tor_generator_;
  std::unique_ptr<AsdrLogger> asdr_logger_;
  std::unique_ptr<ModuleHealthMonitor> health_monitor_;

  rclcpp::Subscription<l3_msgs::msg::SATData>::SharedPtr sat_sub_;
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_HMI_TRANSPARENCY_BRIDGE_NODE_HPP_
