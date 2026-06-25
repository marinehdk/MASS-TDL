// gnc_bridge_node entry point (Track A A4).
//
// Two rclcpp::Context instances are initialized, each after setting a different
// ROS_DOMAIN_ID via setenv(3) (the only way to assign a domain id to a context
// in ROS2 humble without per-node options). Each context drives its own node(s)
// on its own single-threaded executor on its own thread. A CrossDomainHandoff
// queue connects them in-process.
//
// Domain assignment:
//   - L3 domain (42): L3SideNode (subs L3 inputs) + L3PublisherNode (pubs L3)
//   - GNC domain (50): GncSideNode (subs GNC + pubs GNC)
//
// The setenv calls must happen BEFORE each context's init(), because rcl reads
// ROS_DOMAIN_ID from the environment at init time and binds it to the context.
// After init the env var can change for the next context without affecting the
// already-initialized one.
#include <csignal>
#include <cstdlib>
#include <memory>
#include <thread>

#include "rclcpp/rclcpp.hpp"

#include "gnc_bridge/gnc_bridge_node.hpp"

static std::atomic<bool> g_running{true};

static void on_signal(int /*sig*/) { g_running.store(false); }

int main(int argc, char** argv) {
  // --- Context A: L3 domain (42) -------------------------------------------
  setenv("ROS_DOMAIN_ID", "42", 1);
  auto ctx_l3 = std::make_shared<rclcpp::Context>();
  ctx_l3->init(argc, argv);

  rclcpp::NodeOptions opts_l3;
  opts_l3.context(ctx_l3);

  auto handoff = std::make_shared<gnc_bridge::CrossDomainHandoff>();

  auto node_l3_sub  = std::make_shared<gnc_bridge::L3SideNode>(handoff, opts_l3);
  auto node_l3_pub  = std::make_shared<gnc_bridge::L3PublisherNode>(handoff, opts_l3);

  // --- Context B: GNC domain (50) ------------------------------------------
  setenv("ROS_DOMAIN_ID", "50", 1);
  auto ctx_gnc = std::make_shared<rclcpp::Context>();
  ctx_gnc->init(argc, argv);

  rclcpp::NodeOptions opts_gnc;
  opts_gnc.context(ctx_gnc);

  auto node_gnc = std::make_shared<gnc_bridge::GncSideNode>(handoff, opts_gnc);

  // --- Spin each context on its own thread ---------------------------------
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  rclcpp::ExecutorOptions opts_l3_exec;
  opts_l3_exec.context = ctx_l3;
  rclcpp::executors::SingleThreadedExecutor exec_l3(opts_l3_exec);
  exec_l3.add_node(node_l3_sub);
  exec_l3.add_node(node_l3_pub);

  rclcpp::ExecutorOptions opts_gnc_exec;
  opts_gnc_exec.context = ctx_gnc;
  rclcpp::executors::SingleThreadedExecutor exec_gnc(opts_gnc_exec);
  exec_gnc.add_node(node_gnc);

  // Each thread blocks in spin_some until g_running is cleared by the signal
  // handler. We avoid rclcpp::ok(ctx) in the loop predicate because with two
  // independently-init'd contexts the global ok() check can return false even
  // when each context is healthy, prematurely exiting the spin loop.
  std::thread t_l3([&exec_l3]() {
    while (g_running.load()) {
      exec_l3.spin_some(std::chrono::milliseconds(10));
    }
  });
  std::thread t_gnc([&exec_gnc]() {
    while (g_running.load()) {
      exec_gnc.spin_some(std::chrono::milliseconds(10));
    }
  });

  t_l3.join();
  t_gnc.join();

  handoff->shutdown();
  exec_l3.cancel();
  exec_gnc.cancel();
  rclcpp::shutdown(ctx_l3);
  rclcpp::shutdown(ctx_gnc);
  return 0;
}
