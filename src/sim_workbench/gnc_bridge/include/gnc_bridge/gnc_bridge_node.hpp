#ifndef GNC_BRIDGE_GNC_BRIDGE_NODE_HPP_
#define GNC_BRIDGE_GNC_BRIDGE_NODE_HPP_
// gnc_bridge_node — cross-domain bridge between the L3 bus (domain 42) and the
// GNC stack (domain 50). Two rclcpp::Context instances, each with its own
// ROS_DOMAIN_ID, drive two Node subclasses; a thread-safe handoff queue carries
// translated messages between the two executors (each spun on its own thread).
//
// This is the documented ROS2 humble in-process domain-bridging pattern. If the
// two-context pattern proves unstable at runtime, the fallback (spec A4 Step 7)
// is two separate processes connected by a loopback topic; that fallback does
// NOT change the translator layer, only how the two halves are wired.
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

#include "rclcpp/rclcpp.hpp"

#include "gnc_bridge/translators.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_external_msgs/msg/gnc_execution_status.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "ship_interfaces/msg/avoidance_plan.hpp"
#include "ship_interfaces/msg/geo_position.hpp"
#include "ship_interfaces/msg/gnc_execution_odd.hpp"
#include "ship_interfaces/msg/route_execution_status.hpp"
#include "ship_interfaces/msg/route_plan.hpp"
#include "ship_interfaces/msg/ship_reset.hpp"
#include "sil_msgs/msg/own_ship_state.hpp"
#include "sil_msgs/msg/ship_reset.hpp"

namespace gnc_bridge {

inline rclcpp::QoS latched_reset_qos() {
  return rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local();
}

// Thread-safe multi-producer/multi-consumer queue of variant-typed ROS messages
// crossing the L3<->GNC domain boundary inside one process. Each side pushes
// translated messages it has produced; the other side pops and publishes.
class CrossDomainHandoff {
 public:
  // L3 -> GNC payloads.
  struct L3ToGnc {
    ship_interfaces::msg::AvoidancePlan avoidance_plan;
    bool has_avoidance{false};
    ship_interfaces::msg::RoutePlan route_plan;
    bool has_route{false};
    ship_interfaces::msg::ShipReset ship_reset;
    bool has_reset{false};
  };
  // GNC -> L3 payloads.
  struct GncToL3 {
    sil_msgs::msg::OwnShipState own_ship;
    bool has_own_ship{false};
    l3_external_msgs::msg::GncExecutionStatus exec_status;
    bool has_exec_status{false};
    ship_interfaces::msg::GncExecutionOdd execution_odd;  // W2: GNC execution ODD contract
    bool has_execution_odd{false};
  };

  void push_l3_to_gnc(L3ToGnc msg) {
    {
      std::lock_guard<std::mutex> lk(l3_mtx_);
      l3_queue_.push_back(std::move(msg));
    }
    l3_cv_.notify_one();
  }
  void push_gnc_to_l3(GncToL3 msg) {
    {
      std::lock_guard<std::mutex> lk(gnc_mtx_);
      gnc_queue_.push_back(std::move(msg));
    }
    gnc_cv_.notify_one();
  }

  // Block until an L3->GNC item is available, then pop it. Returns false if
  // shutdown was requested.
  bool pop_l3_to_gnc(L3ToGnc& out) {
    std::unique_lock<std::mutex> lk(l3_mtx_);
    l3_cv_.wait(lk, [&] { return !l3_queue_.empty() || shutdown_.load(); });
    if (shutdown_.load() && l3_queue_.empty()) return false;
    out = std::move(l3_queue_.front());
    l3_queue_.pop_front();
    return true;
  }
  bool pop_gnc_to_l3(GncToL3& out) {
    std::unique_lock<std::mutex> lk(gnc_mtx_);
    gnc_cv_.wait(lk, [&] { return !gnc_queue_.empty() || shutdown_.load(); });
    if (shutdown_.load() && gnc_queue_.empty()) return false;
    out = std::move(gnc_queue_.front());
    gnc_queue_.pop_front();
    return true;
  }

  // Non-blocking variants for drain timers. The blocking pop_* variants above
  // MUST NOT be called from an executor-spun callback: they wait on a condition
  // variable, which would stall the executor thread and starve subscription
  // callbacks. The drain timers (run on the executor) use these try_* variants
  // instead — they return immediately (false) when the queue is empty.
  bool try_pop_l3_to_gnc(L3ToGnc& out) {
    std::lock_guard<std::mutex> lk(l3_mtx_);
    if (l3_queue_.empty()) return false;
    out = std::move(l3_queue_.front());
    l3_queue_.pop_front();
    return true;
  }
  bool try_pop_gnc_to_l3(GncToL3& out) {
    std::lock_guard<std::mutex> lk(gnc_mtx_);
    if (gnc_queue_.empty()) return false;
    out = std::move(gnc_queue_.front());
    gnc_queue_.pop_front();
    return true;
  }

  void shutdown() {
    shutdown_.store(true);
    l3_cv_.notify_all();
    gnc_cv_.notify_all();
  }

 private:
  std::mutex l3_mtx_, gnc_mtx_;
  std::condition_variable l3_cv_, gnc_cv_;
  std::deque<L3ToGnc> l3_queue_;
  std::deque<GncToL3> gnc_queue_;
  std::atomic<bool> shutdown_{false};
};

// L3-domain node (domain 42). Subscribes the L3 canonical topics and translates
// them into GNC types before pushing to the handoff for the GNC side to publish.
class L3SideNode : public rclcpp::Node {
 public:
  explicit L3SideNode(std::shared_ptr<CrossDomainHandoff> handoff,
                      const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
 private:
  std::shared_ptr<CrossDomainHandoff> handoff_;
  rclcpp::Subscription<l3_msgs::msg::AvoidancePlan>::SharedPtr sub_avoidance_;
  rclcpp::TimerBase::SharedPtr avoidance_watchdog_timer_;
  std::optional<rclcpp::Time> last_avoidance_plan_wall_time_;
  rclcpp::Subscription<l3_external_msgs::msg::PlannedRoute>::SharedPtr sub_route_;
  rclcpp::Subscription<sil_msgs::msg::ShipReset>::SharedPtr sub_reset_;
};

// GNC-domain node (domain 50). Subscribes the GNC ship_interfaces topics and
// translates them into L3 types before pushing to the handoff for the L3 side
// to publish. It also drains L3->GNC items and publishes them on the GNC domain.
class GncSideNode : public rclcpp::Node {
 public:
  explicit GncSideNode(std::shared_ptr<CrossDomainHandoff> handoff,
                       const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
 private:
  std::shared_ptr<CrossDomainHandoff> handoff_;
  rclcpp::Subscription<ship_interfaces::msg::GeoPosition>::SharedPtr sub_geo_;
  rclcpp::Subscription<ship_interfaces::msg::RouteExecutionStatus>::SharedPtr sub_status_;
  rclcpp::Subscription<ship_interfaces::msg::GncExecutionOdd>::SharedPtr sub_odd_;  // W2
  rclcpp::Publisher<ship_interfaces::msg::AvoidancePlan>::SharedPtr pub_avoidance_;
  rclcpp::Publisher<ship_interfaces::msg::RoutePlan>::SharedPtr pub_route_;
  rclcpp::Publisher<ship_interfaces::msg::ShipReset>::SharedPtr pub_geo_reset_;
  rclcpp::Publisher<ship_interfaces::msg::ShipReset>::SharedPtr pub_dynamics_reset_;
  rclcpp::TimerBase::SharedPtr drain_timer_;
};

// L3-side publishers live on a second L3 node so the L3->GNC subscription and
// the GNC->L3 publishing do not need to share one node's executor.
class L3PublisherNode : public rclcpp::Node {
 public:
  explicit L3PublisherNode(std::shared_ptr<CrossDomainHandoff> handoff,
                           const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
 private:
  std::shared_ptr<CrossDomainHandoff> handoff_;
  rclcpp::Publisher<sil_msgs::msg::OwnShipState>::SharedPtr pub_own_ship_;
  rclcpp::Publisher<l3_external_msgs::msg::GncExecutionStatus>::SharedPtr pub_exec_status_;
  rclcpp::Publisher<ship_interfaces::msg::GncExecutionOdd>::SharedPtr pub_odd_;  // W2
  rclcpp::TimerBase::SharedPtr drain_timer_;
};

}  // namespace gnc_bridge

#endif  // GNC_BRIDGE_GNC_BRIDGE_NODE_HPP_
