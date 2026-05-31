#include <memory>
#include <rclcpp/rclcpp.hpp>

#include "m1_odd_envelope_manager/odd_envelope_manager_node.hpp"
#include "m2_world_model/world_model_node.hpp"
#include "m3_mission_manager/mission_manager_node.hpp"
#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_node.hpp"
#include "m6_colregs_reasoner/colregs_reasoner_node.hpp"
#include "m8_hmi_transparency_bridge/hmi_transparency_bridge_node.hpp"

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);

  rclcpp::NodeOptions options_with_ipc;
  options_with_ipc.use_intra_process_comms(true);

  rclcpp::NodeOptions options_without_ipc;
  options_without_ipc.use_intra_process_comms(false);

  // Instantiate the 7 DOER nodes manually
  // M1: OddEnvelopeManagerNode (no-arg constructor)
  auto m1_node = std::make_shared<mass_l3::m1::OddEnvelopeManagerNode>();

  // M2: WorldModelNode (with NodeOptions)
  auto m2_node = std::make_shared<mass_l3::m2::WorldModelNode>(options_without_ipc);

  // M3: MissionManagerNode (with NodeOptions)
  auto m3_node = std::make_shared<mass_l3::m3::MissionManagerNode>(options_without_ipc);

  // M4: BehaviorArbiterNode (with NodeOptions)
  auto m4_node = std::make_shared<mass_l3::m4::BehaviorArbiterNode>(options_with_ipc);

  // M5: MidMpcNode (with default config)
  mass_l3::m5::mid_mpc::MidMpcNode::Config m5_cfg;
  auto m5_node = std::make_shared<mass_l3::m5::mid_mpc::MidMpcNode>(m5_cfg);

  // M6: ColregsReasonerNode (no-arg constructor)
  auto m6_node = std::make_shared<mass_l3::m6_colregs::ColregsReasonerNode>();

  // M8: HmiTransparencyBridgeNode (with NodeOptions)
  auto m8_node = std::make_shared<mass_l3::m8::HmiTransparencyBridgeNode>(options_without_ipc);

  // Add nodes to a single-threaded executor
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(m1_node);
  executor.add_node(m2_node);
  executor.add_node(m3_node);
  executor.add_node(m4_node);
  executor.add_node(m5_node);
  executor.add_node(m6_node);
  executor.add_node(m8_node);

  RCLCPP_INFO(rclcpp::get_logger("doer_composition"), 
              "All 7 DOER nodes successfully composed and added to SingleThreadedExecutor. Spinning...");

  executor.spin();

  rclcpp::shutdown();
  return 0;
}
