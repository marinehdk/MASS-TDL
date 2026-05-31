#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <rcl/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <vector>
#include <iostream>
#include <cstdlib>
#include <cstdio>

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

  const char* headless_env = std::getenv("SIL_HEADLESS");
  bool headless = (headless_env && std::string(headless_env) == "1");

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
  std::shared_ptr<mass_l3::m8::HmiTransparencyBridgeNode> m8_node = nullptr;
  if (!headless) {
    m8_node = std::make_shared<mass_l3::m8::HmiTransparencyBridgeNode>(options_without_ipc);
  }

  // Add nodes to a single-threaded executor wrapped in a shared pointer
  auto executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
  executor->add_node(m1_node);
  executor->add_node(m2_node);
  executor->add_node(m3_node);
  executor->add_node(m4_node);
  executor->add_node(m5_node);
  executor->add_node(m6_node);
  if (m8_node) {
    executor->add_node(m8_node);
  }

  const char* lockstep_port_env = std::getenv("SIL_LOCKSTEP_PORT");
  if (lockstep_port_env && std::string(lockstep_port_env) != "") {
    int port = 0;
    try {
      port = std::stoi(lockstep_port_env);
    } catch (const std::exception& e) {
      RCLCPP_ERROR(rclcpp::get_logger("doer_composition"), "Invalid SIL_LOCKSTEP_PORT: %s", e.what());
      return 1;
    }
    RCLCPP_INFO(rclcpp::get_logger("doer_composition"), "Lockstep mode enabled on port %d", port);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
      RCLCPP_ERROR(rclcpp::get_logger("doer_composition"), "Socket creation error");
      return 1;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(static_cast<uint16_t>(port));

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
      RCLCPP_ERROR(rclcpp::get_logger("doer_composition"), "Invalid address/Address not supported");
      close(sock);
      return 1;
    }

    RCLCPP_INFO(rclcpp::get_logger("doer_composition"), "Connecting to lockstep coordinator...");
    while (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
      RCLCPP_WARN(rclcpp::get_logger("doer_composition"), "Connection failed, retrying in 500ms...");
      usleep(500000);
    }
    RCLCPP_INFO(rclcpp::get_logger("doer_composition"), "Connected to lockstep coordinator!");

    std::vector<std::shared_ptr<rclcpp::Node>> nodes = {
      m1_node, m2_node, m3_node, m4_node, m5_node, m6_node
    };
    if (m8_node) {
      nodes.push_back(m8_node);
    }

    for (auto const& node : nodes) {
      rcl_enable_ros_time_override(node->get_clock()->get_clock_handle());
    }

    std::string buffer;
    char c;
    while (true) {
      buffer.clear();
      bool ok = true;
      while (true) {
        ssize_t n = read(sock, &c, 1);
        if (n <= 0) {
          ok = false;
          break;
        }
        if (c == '\n') {
          break;
        }
        buffer += c;
      }
      if (!ok) {
        RCLCPP_WARN(rclcpp::get_logger("doer_composition"), "Coordinator disconnected. Exiting step loop.");
        break;
      }

      if (buffer.rfind("STEP ", 0) == 0) {
        int64_t sec = 0;
        int64_t nanosec = 0;
        if (std::sscanf(buffer.c_str(), "STEP %ld %ld", &sec, &nanosec) == 2) {
          uint64_t time_value = sec * 1000000000ULL + nanosec;
          for (auto const& node : nodes) {
            rcl_set_ros_time_override(node->get_clock()->get_clock_handle(), time_value);
          }

          executor->spin_some(std::chrono::milliseconds(0));

          std::string ack = "ACK " + std::to_string(sec) + " " + std::to_string(nanosec) + "\n";
          if (write(sock, ack.c_str(), ack.size()) < 0) {
            RCLCPP_ERROR(rclcpp::get_logger("doer_composition"), "Failed to send ACK to coordinator");
            break;
          }
        }
      } else if (buffer.rfind("RESET ", 0) == 0) {
        int64_t seed = 0;
        int64_t episode = 0;
        if (std::sscanf(buffer.c_str(), "RESET %ld %ld", &seed, &episode) == 2) {
          // Remove the 7 nodes from the executor
          for (auto const& node : nodes) {
            executor->remove_node(node);
          }
          nodes.clear();

          // Explicitly destroy the old nodes and executor
          m1_node.reset();
          m2_node.reset();
          m3_node.reset();
          m4_node.reset();
          m5_node.reset();
          m6_node.reset();
          if (m8_node) {
            m8_node.reset();
          }
          executor.reset();

          // Re-instantiate executor and nodes
          executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
          m1_node = std::make_shared<mass_l3::m1::OddEnvelopeManagerNode>();
          m2_node = std::make_shared<mass_l3::m2::WorldModelNode>(options_without_ipc);
          m3_node = std::make_shared<mass_l3::m3::MissionManagerNode>(options_without_ipc);
          m4_node = std::make_shared<mass_l3::m4::BehaviorArbiterNode>(options_with_ipc);
          m5_node = std::make_shared<mass_l3::m5::mid_mpc::MidMpcNode>(m5_cfg);
          m6_node = std::make_shared<mass_l3::m6_colregs::ColregsReasonerNode>();
          if (!headless) {
            m8_node = std::make_shared<mass_l3::m8::HmiTransparencyBridgeNode>(options_without_ipc);
          }

          // Re-assign the references in the nodes vector
          nodes = {
            m1_node, m2_node, m3_node, m4_node, m5_node, m6_node
          };
          if (m8_node) {
            nodes.push_back(m8_node);
          }

          // Re-enable ROS time override and set it to 0 for fresh node clocks
          for (auto const& node : nodes) {
            rcl_enable_ros_time_override(node->get_clock()->get_clock_handle());
            rcl_set_ros_time_override(node->get_clock()->get_clock_handle(), 0ULL);
          }

          // Add the fresh nodes back to the executor
          for (auto const& node : nodes) {
            executor->add_node(node);
          }

          // Send ACK_RESET back to the socket
          std::string ack = "ACK_RESET " + std::to_string(seed) + " " + std::to_string(episode) + "\n";
          if (write(sock, ack.c_str(), ack.size()) < 0) {
            RCLCPP_ERROR(rclcpp::get_logger("doer_composition"), "Failed to send ACK_RESET to coordinator");
            break;
          }
        }
      }
    }
    close(sock);
  } else {
    RCLCPP_INFO(rclcpp::get_logger("doer_composition"), 
                "All 7 DOER nodes successfully composed and added to SingleThreadedExecutor. Spinning...");
    executor->spin();
  }

  rclcpp::shutdown();
  return 0;
}
