// gnc_bridge_node entry point (Track A A4).
//
// Two rclcpp::Context instances, each initialized with its own
// rclcpp::InitOptions::set_domain_id(), drive nodes on two DDS domains in one
// process. Each context gets its own Cyclone DDS participant bound to its
// domain. A thread-safe CrossDomainHandoff queue carries translated messages
// between the two sides.
//
// Domain assignment:
//   - L3 domain (42): L3SideNode (subs L3 inputs) + L3PublisherNode (pubs L3)
//   - GNC domain (50): GncSideNode (subs GNC + pubs GNC)
//
// Init pattern follows ros2/domain_bridge (the canonical single-process
// domain bridge): a global rclcpp::init creates the default context, then each
// domain gets its own Context via InitOptions::set_domain_id() with
// auto_initialize_logging(false). Nodes are created with NodeOptions bound to
// their context (plus use_global_arguments(false) etc. per domain_bridge). All
// nodes run under ONE MultiThreadedExecutor, which is how domain_bridge spins
// nodes belonging to different contexts in a single process.
//
// Earlier attempts that failed: (a) setenv("ROS_DOMAIN_ID") — shares one
// Cyclone participant, second setenv retroactively rebinds it; (b) two
// SingleThreadedExecutors each bound via ExecutorOptions.context() to one
// context — the per-executor context binding prevented cross-context data
// delivery on the L3 side. The single MultiThreadedExecutor + per-node
// NodeOptions.context() pattern is the proven approach.
#include <csignal>
#include <memory>

#include "rclcpp/rclcpp.hpp"

#include "gnc_bridge/gnc_bridge_node.hpp"

static std::atomic<bool> g_running{true};

static void on_signal(int /*sig*/) { g_running.store(false); }

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);  // global default context (logging init happens here)

  // --- Context A: L3 domain (42) -------------------------------------------
  rclcpp::InitOptions opts_l3_init;
  opts_l3_init.auto_initialize_logging(false).set_domain_id(42);
  auto ctx_l3 = std::make_shared<rclcpp::Context>();
  ctx_l3->init(argc, argv, opts_l3_init);

  // --- Context B: GNC domain (50) ------------------------------------------
  rclcpp::InitOptions opts_gnc_init;
  opts_gnc_init.auto_initialize_logging(false).set_domain_id(50);
  auto ctx_gnc = std::make_shared<rclcpp::Context>();
  ctx_gnc->init(argc, argv, opts_gnc_init);

  // NodeOptions per domain_bridge's create_node_options: context-bound, no
  // global arguments, no parameter services/event publisher.
  auto make_opts = [](rclcpp::Context::SharedPtr ctx) {
    rclcpp::NodeOptions o;
    o.context(ctx)
        .use_global_arguments(false)
        .start_parameter_services(false)
        .start_parameter_event_publisher(false);
    return o;
  };

  auto handoff = std::make_shared<gnc_bridge::CrossDomainHandoff>();

  auto node_l3_sub = std::make_shared<gnc_bridge::L3SideNode>(handoff, make_opts(ctx_l3));
  auto node_l3_pub = std::make_shared<gnc_bridge::L3PublisherNode>(handoff, make_opts(ctx_l3));
  auto node_gnc    = std::make_shared<gnc_bridge::GncSideNode>(handoff, make_opts(ctx_gnc));

  // One MultiThreadedExecutor spins all nodes (across both contexts), matching
  // the domain_bridge pattern. Multi-threaded so the per-side drain timers and
  // subscriptions do not starve each other.
  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(node_l3_sub);
  executor.add_node(node_l3_pub);
  executor.add_node(node_gnc);

  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  // Spin on a thread; the main thread waits for shutdown.
  std::thread spin_thread([&executor]() {
    while (g_running.load()) {
      executor.spin_some(std::chrono::milliseconds(10));
    }
  });

  spin_thread.join();

  handoff->shutdown();
  executor.cancel();
  rclcpp::shutdown(ctx_l3);
  rclcpp::shutdown(ctx_gnc);
  rclcpp::shutdown();
  return 0;
}
