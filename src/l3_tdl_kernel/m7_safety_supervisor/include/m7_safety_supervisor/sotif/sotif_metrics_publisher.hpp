#ifndef M7_SAFETY_SUPERVISOR_SOTIF_SOTIF_METRICS_PUBLISHER_HPP_
#define M7_SAFETY_SUPERVISOR_SOTIF_SOTIF_METRICS_PUBLISHER_HPP_

#include <rclcpp/rclcpp.hpp>
#include "l3_msgs/msg/sotif_metrics.hpp"
#include "m7_safety_supervisor/sotif/assumption_monitor.hpp"

namespace mass_l3::m7::sotif {

class SotifMetricsPublisher {
public:
  explicit SotifMetricsPublisher(rclcpp::Node* node);

  void publish(AssumptionStatus const& status, std::uint16_t veto_window_count) noexcept;

  void set_stub_mode(bool enabled) noexcept;

private:
  rclcpp::Publisher<l3_msgs::msg::SotifMetrics>::SharedPtr publisher_;
  l3_msgs::msg::SotifMetrics cached_msg_;  // pre-allocated; reused every cycle
  bool stub_mode_{true};
};

}  // namespace mass_l3::m7::sotif

#endif  // M7_SAFETY_SUPERVISOR_SOTIF_SOTIF_METRICS_PUBLISHER_HPP_
