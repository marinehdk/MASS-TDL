#pragma once

#include <cstdint>
#include <string>

#include <rclcpp/rclcpp.hpp>

namespace mass_l3::m2 {

/// Declare all M2 World Model ROS2 parameters from the node.
/// Call from WorldModelNode::load_parameters() before create_components().
inline void declare_m2_parameters(rclcpp::Node& node) {
  // ── Rates ──
  node.declare_parameter("aggregation_rate_hz", 4.0);
  node.declare_parameter("sat_rate_hz", 10.0);
  node.declare_parameter("asdr_periodic_rate_hz", 2.0);
  node.declare_parameter("heartbeat_rate_hz", 1.0);

  // ── Buffer / cache ──
  node.declare_parameter("max_targets", 256);
  node.declare_parameter("target_disappearance_periods", 10);
  node.declare_parameter("environment_cache_ttl_s", 60.0);

  // ── CPA safe thresholds (array of 4, one per OddZone A/B/C/D) ──
  node.declare_parameter("cpa_safe_m", std::vector<double>{1852.0, 555.6, 277.8, 2778.0});
  node.declare_parameter("tcpa_safe_s", std::vector<double>{720.0, 240.0, 180.0, 600.0});

  // ── COLREG geometry ──
  node.declare_parameter("overtaking_bearing_min_deg", 112.5);
  node.declare_parameter("overtaking_bearing_max_deg", 247.5);
  node.declare_parameter("head_on_heading_diff_tol_deg", 6.0);
  node.declare_parameter("safe_pass_speed_threshold_mps", 0.5);
  node.declare_parameter("safe_pass_min_cpa_m", 926.0);

  // ── View health thresholds ──
  node.declare_parameter("dv_loss_periods_to_degraded", 2);
  node.declare_parameter("dv_loss_periods_to_critical", 5);
  node.declare_parameter("ev_loss_ms_to_critical", 100.0);
  node.declare_parameter("sv_loss_s_to_degraded", 30.0);

  // ── ENC ──
  node.declare_parameter("enc_data_root", std::string(""));
  node.declare_parameter("enc_metadata_file", std::string(""));
}

}  // namespace mass_l3::m2
