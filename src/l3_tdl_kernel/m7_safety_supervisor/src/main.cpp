#include "m7_safety_supervisor/safety_supervisor_node.hpp"

#include <memory>
#include <rclcpp/executors/multi_threaded_executor.hpp>
#include <rclcpp/node_options.hpp>
#include <rclcpp/utilities.hpp>
#include <rclcpp/logging.hpp>
#include <rcl/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <cstdlib>
#include <cstdio>

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::NodeOptions const kOpts;
  auto node = std::make_shared<mass_l3::m7::SafetySupervisorNode>(kOpts);
  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(node);

  const char* lockstep_port_env = std::getenv("SIL_LOCKSTEP_PORT");
  if (lockstep_port_env && std::string(lockstep_port_env) != "") {
    int port = std::stoi(lockstep_port_env);
    RCLCPP_INFO(node->get_logger(), "Lockstep mode enabled on port %d", port);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
      RCLCPP_ERROR(node->get_logger(), "Socket creation error");
      return 1;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(static_cast<uint16_t>(port));

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
      RCLCPP_ERROR(node->get_logger(), "Invalid address/Address not supported");
      close(sock);
      return 1;
    }

    RCLCPP_INFO(node->get_logger(), "Connecting to lockstep coordinator...");
    while (connect(sock, reinterpret_cast<struct sockaddr*>(&serv_addr), sizeof(serv_addr)) < 0) {
      RCLCPP_WARN(node->get_logger(), "Connection failed, retrying in 500ms...");
      usleep(500000);
    }
    RCLCPP_INFO(node->get_logger(), "Connected to lockstep coordinator!");

    auto ret_enable = rcl_enable_ros_time_override(node->get_clock()->get_clock_handle());
    (void)ret_enable;

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
        RCLCPP_WARN(node->get_logger(), "Coordinator disconnected. Exiting step loop.");
        break;
      }

      if (buffer.rfind("STEP ", 0) == 0) {
        int64_t sec = 0;
        int64_t nanosec = 0;
        if (std::sscanf(buffer.c_str(), "STEP %ld %ld", &sec, &nanosec) == 2) {
          int64_t time_value = sec * 1000000000L + nanosec;
          auto ret_set = rcl_set_ros_time_override(node->get_clock()->get_clock_handle(), time_value);
          (void)ret_set;

          executor.spin_some(std::chrono::milliseconds(0));

          std::string ack = "ACK " + std::to_string(sec) + " " + std::to_string(nanosec) + "\n";
          if (write(sock, ack.c_str(), ack.size()) < 0) {
            RCLCPP_ERROR(node->get_logger(), "Failed to send ACK to coordinator");
            break;
          }
        }
      }
    }
    close(sock);
  } else {
    RCLCPP_INFO(node->get_logger(), "Spinning in standard (non-lockstep) mode...");
    executor.spin();
  }

  rclcpp::shutdown();
  return 0;
}
