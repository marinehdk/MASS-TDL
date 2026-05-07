#include "m8_hmi_transparency_bridge/hmi_transparency_bridge_node.hpp"

namespace mass_l3::m8 {

HmiTransparencyBridgeNode::HmiTransparencyBridgeNode(
    const rclcpp::NodeOptions& options)
    : rclcpp::Node{"m8_hmi_transparency_bridge", options},
      aggregator_{std::make_unique<SatAggregator>()},
      sat_trigger_{std::make_unique<AdaptiveSatTrigger>()},
      tor_protocol_{std::make_unique<TorProtocol>(TorProtocol::Config{})},
      ui_builder_{std::make_unique<UiStateBuilder>()},
      tor_generator_{std::make_unique<TorRequestGenerator>()},
      asdr_logger_{std::make_unique<AsdrLogger>()},
      health_monitor_{std::make_unique<ModuleHealthMonitor>()}
{
  sat_sub_ = create_subscription<l3_msgs::msg::SATData>(
      "/l3/sat_data",
      rclcpp::QoS{10},
      [this](const l3_msgs::msg::SATData::SharedPtr msg) {
        on_sat_data(msg);
      });
}

void HmiTransparencyBridgeNode::on_sat_data(
    const l3_msgs::msg::SATData::SharedPtr msg)
{
  aggregator_->ingest(*msg, SatAggregator::Clock::now());
}

}  // namespace mass_l3::m8
