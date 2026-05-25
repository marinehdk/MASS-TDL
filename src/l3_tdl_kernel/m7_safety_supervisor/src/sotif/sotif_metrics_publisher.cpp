#include "m7_safety_supervisor/sotif/sotif_metrics_publisher.hpp"
#include <cstddef>
#include <cstdint>

namespace mass_l3::m7::sotif {

namespace {
  constexpr std::size_t kIdxAis    = static_cast<std::size_t>(AssumptionId::kAisRadarConsistency);
  constexpr std::size_t kIdxMotion = static_cast<std::size_t>(AssumptionId::kMotionPredictability);
  constexpr std::size_t kIdxCovg   = static_cast<std::size_t>(AssumptionId::kPerceptionCoverage);
  constexpr std::size_t kIdxColreg = static_cast<std::size_t>(AssumptionId::kColregsSolvability);
  constexpr std::size_t kIdxComm   = static_cast<std::size_t>(AssumptionId::kCommLink);
  constexpr std::size_t kIdxVeto   = static_cast<std::size_t>(AssumptionId::kCheckerVetoRate);
}  // namespace

SotifMetricsPublisher::SotifMetricsPublisher(rclcpp::Node* node)
  : publisher_{node->create_publisher<l3_msgs::msg::SotifMetrics>(
      "/sil/sotif_metrics", rclcpp::QoS(10).best_effort())} {}

void SotifMetricsPublisher::publish(AssumptionStatus const& status,
                                     std::uint16_t veto_window_count) noexcept {
  l3_msgs::msg::SotifMetrics msg;
  msg.schema_version = 113;
  msg.metrics.resize(6);

  for (std::uint8_t i = 0; i < 6; ++i) {
    auto& entry = msg.metrics[i];
    entry.assumption_id = i;
    entry.is_violated = status.violation_active[i];

    if (stub_mode_) {
      entry.violation_score = 0.0F;
      entry.window_count    = 0;
      entry.raw_value       = 0.0F;
    } else {
      entry.violation_score = status.violation_metric[i];
      entry.raw_value       = status.violation_metric[i];
      entry.window_count = (i == kIdxVeto) ? veto_window_count : 0;
    }
  }

  publisher_->publish(msg);
}

void SotifMetricsPublisher::set_stub_mode(bool enabled) noexcept {
  stub_mode_ = enabled;
}

}  // namespace mass_l3::m7::sotif
