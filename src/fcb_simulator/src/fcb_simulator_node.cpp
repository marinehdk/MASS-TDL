// SPDX-License-Identifier: Proprietary
// FCB simulator ROS2 node — drives the MMG model from M5 commands and
// publishes FilteredOwnShipState (50 Hz) + TrackedTargetArray (2 Hz).
#include "fcb_simulator/fcb_simulator_node.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <ament_index_cpp/get_package_share_directory.hpp>

namespace fcb_sim {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kKnotsToMps = 0.514444;
constexpr double kMpsToKnots = 1.0 / kKnotsToMps;
constexpr double kRadToDeg = 180.0 / kPi;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kEarthMperDeg = 111320.0;

double wrap_pi(double angle) {
  while (angle >  kPi) { angle -= 2.0 * kPi; }
  while (angle < -kPi) { angle += 2.0 * kPi; }
  return angle;
}

}  // namespace

FcbSimulatorNode::FcbSimulatorNode()
    : rclcpp::Node("fcb_simulator"),
      override_expires_(0, 0, RCL_ROS_TIME) {
  load_params();

  state_.x = cfg_.x0;
  state_.y = cfg_.y0;
  state_.psi = cfg_.psi0;
  state_.u = cfg_.u0;
  psi_target_rad_ = cfg_.psi0;
  u_target_mps_   = cfg_.u0;

  sub_plan_ = create_subscription<l3_msgs::msg::AvoidancePlan>(
      "/m5/avoidance_plan", 10,
      std::bind(&FcbSimulatorNode::on_avoidance_plan, this, std::placeholders::_1));
  sub_override_ = create_subscription<l3_msgs::msg::ReactiveOverrideCmd>(
      "/m5/reactive_override_cmd", 10,
      std::bind(&FcbSimulatorNode::on_reactive_override, this, std::placeholders::_1));

  pub_state_ = create_publisher<l3_external_msgs::msg::FilteredOwnShipState>(
      "/fusion/own_ship_state", rclcpp::SensorDataQoS());
  pub_targets_ = create_publisher<l3_external_msgs::msg::TrackedTargetArray>(
      "/fusion/tracked_targets", 10);

  using namespace std::chrono_literals;
  const auto period_dyn = std::chrono::duration<double>(cfg_.dt);
  timer_dynamics_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::nanoseconds>(period_dyn),
      [this]() {
        step_dynamics();
        publish_own_ship_state();
      });
  timer_targets_ = create_wall_timer(
      500ms, [this]() { publish_tracked_targets(); });

  RCLCPP_INFO(get_logger(),
              "fcb_simulator started: dt=%.3fs, u0=%.2f m/s",
              cfg_.dt, cfg_.u0);
}

void FcbSimulatorNode::load_params() {
  cfg_.dt         = declare_parameter("dt",         0.02);
  cfg_.x0         = declare_parameter("x0",         0.0);
  cfg_.y0         = declare_parameter("y0",         0.0);
  cfg_.psi0       = declare_parameter("psi0",       1.5708);
  cfg_.u0         = declare_parameter("u0",         9.26);
  cfg_.origin_lat = declare_parameter("origin_lat", 30.5);
  cfg_.origin_lon = declare_parameter("origin_lon", 122.0);

  std::string vessel_class = declare_parameter("vessel_class", std::string("FCB"));
  std::string plugin_name  = "fcb_simulator/" + vessel_class + "Simulator";

  try {
    class_loader_ = std::make_unique<pluginlib::ClassLoader<ship_sim::ShipMotionSimulator>>(
        "ship_sim_interfaces", "ship_sim::ShipMotionSimulator");
    plugin_ = class_loader_->createSharedInstance(plugin_name);
  } catch (const pluginlib::PluginlibException& ex) {
    RCLCPP_FATAL(get_logger(), "Cannot load plugin '%s': %s", plugin_name.c_str(), ex.what());
    throw;
  }

  try {
    std::string config_path = ament_index_cpp::get_package_share_directory("fcb_simulator")
                              + "/config/fcb_dynamics.yaml";
    plugin_->load_params(config_path);
    RCLCPP_INFO(get_logger(), "Loaded plugin '%s'", plugin_name.c_str());
  } catch (const std::exception& ex) {
    RCLCPP_FATAL(get_logger(), "Cannot load plugin params: %s", ex.what());
    throw;
  }
}

void FcbSimulatorNode::on_avoidance_plan(
    const l3_msgs::msg::AvoidancePlan::SharedPtr msg) {
  std::lock_guard<std::mutex> lk(cmd_mutex_);
  if (msg->waypoints.empty()) {
    return;
  }
  // Aim at the first waypoint as heading reference.
  const auto& wp = msg->waypoints.front();
  const double dlat = wp.position.latitude - cfg_.origin_lat;
  const double dlon = wp.position.longitude - cfg_.origin_lon;
  const double tgt_y = dlat * kEarthMperDeg;
  const double tgt_x = dlon * kEarthMperDeg
                       * std::cos(cfg_.origin_lat * kDegToRad);
  // state_ reads are safe under SingleThreadedExecutor (no concurrent timer callbacks)
  const double dx = tgt_x - state_.x;
  const double dy = tgt_y - state_.y;
  if (std::hypot(dx, dy) > 1.0) {
    psi_target_rad_ = std::atan2(dy, dx);
  }
  u_target_mps_ = wp.target_speed_kn * kKnotsToMps;
}

void FcbSimulatorNode::on_reactive_override(
    const l3_msgs::msg::ReactiveOverrideCmd::SharedPtr msg) {
  std::lock_guard<std::mutex> lk(cmd_mutex_);
  override_active_ = true;
  override_psi_rad_ = static_cast<double>(msg->heading_cmd_deg) * kDegToRad;
  override_u_mps_   = static_cast<double>(msg->speed_cmd_kn) * kKnotsToMps;
  override_expires_ = now()
      + rclcpp::Duration::from_seconds(static_cast<double>(msg->validity_s));
}

void FcbSimulatorNode::compute_control(double& delta_rad,
                                       double& n_rps) const {
  std::lock_guard<std::mutex> lk(cmd_mutex_);
  double psi_tgt = psi_target_rad_;
  double u_tgt   = u_target_mps_;
  if (override_active_ && now() < override_expires_) {
    psi_tgt = override_psi_rad_;
    u_tgt   = override_u_mps_;
  }

  // Simple proportional autopilot: heading error → rudder.
  const double psi_err = wrap_pi(psi_tgt - state_.psi);
  constexpr double kPsiKp = 1.5;
  constexpr double kMaxRudderRad = 35.0 * kPi / 180.0;
  delta_rad = std::clamp(kPsiKp * psi_err, -kMaxRudderRad, kMaxRudderRad);

  // Speed governor: solve thrust steady-state for u_tgt (rough P-loop).
  constexpr double kSpeedKp = 0.2;          // rps per (m/s) error
  constexpr double kFeedfwd = 0.6;          // rps per (m/s) commanded
  constexpr double kMaxRps  = 25.0;
  const double u_err = u_tgt - state_.u;
  n_rps = std::clamp(kFeedfwd * u_tgt + kSpeedKp * u_err, 0.0, kMaxRps);
}

void FcbSimulatorNode::step_dynamics() {
  double delta = 0.0;
  double n = 0.0;
  compute_control(delta, n);
  state_ = plugin_->step(state_, delta, n, cfg_.dt);
  constexpr double kPi = 3.14159265358979323846;
  while (state_.psi >  kPi) { state_.psi -= 2.0 * kPi; }
  while (state_.psi < -kPi) { state_.psi += 2.0 * kPi; }
}

void FcbSimulatorNode::publish_own_ship_state() {
  l3_external_msgs::msg::FilteredOwnShipState msg;
  msg.schema_version = "v1.1.2";
  msg.stamp = now();

  // ENU → WGS84 (flat earth)
  msg.position.latitude  = cfg_.origin_lat + state_.y / kEarthMperDeg;
  msg.position.longitude = cfg_.origin_lon
      + state_.x / (kEarthMperDeg * std::cos(cfg_.origin_lat * kDegToRad));
  msg.position.altitude = 0.0;

  // Over-ground velocity
  const double vx_ned = state_.u * std::cos(state_.psi)
                      - state_.v * std::sin(state_.psi);
  const double vy_ned = state_.u * std::sin(state_.psi)
                      + state_.v * std::cos(state_.psi);
  const double speed_mps = std::hypot(vx_ned, vy_ned);
  msg.sog_kn = speed_mps * kMpsToKnots;
  msg.cog_deg = wrap_pi(std::atan2(vy_ned, vx_ned)) * kRadToDeg;
  msg.heading_deg = state_.psi * kRadToDeg;

  msg.u_water = state_.u;
  msg.v_water = state_.v;
  msg.r_dot_deg_s = state_.r * kRadToDeg;  // r = yaw rate — IDL field named r_dot per legacy convention

  msg.current_speed_kn = 0.0;
  msg.current_direction_deg = 0.0;
  msg.roll_deg = state_.phi * kRadToDeg;
  msg.pitch_deg = 0.0;

  for (auto& c : msg.covariance) { c = 0.0; }
  for (size_t i = 0; i < 6; ++i) { msg.covariance[i * 6 + i] = 0.01; }

  msg.nav_mode = "OPTIMAL";
  msg.confidence = 0.99F;
  msg.rationale = "fcb_simulator MMG/RK4 simulated state";
  pub_state_->publish(msg);
}

void FcbSimulatorNode::publish_tracked_targets() {
  l3_external_msgs::msg::TrackedTargetArray msg;
  msg.schema_version = "v1.1.2";
  msg.stamp = now();
  msg.targets.clear();  // Static empty scenario; populate via scenario file later.
  msg.confidence = 0.95F;
  msg.rationale = "fcb_simulator: no targets in default scenario";
  pub_targets_->publish(msg);
}

}  // namespace fcb_sim

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<fcb_sim::FcbSimulatorNode>());
  rclcpp::shutdown();
  return 0;
}
