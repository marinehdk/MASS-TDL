// SPDX-License-Identifier: Proprietary
// FCB simulator ROS2 node — drives the MMG model from M5 commands and
// publishes FilteredOwnShipState (50 Hz) + TrackedTargetArray (2 Hz).
#include "fcb_simulator/fcb_simulator_node.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "fcb_simulator/rk4_integrator.hpp"

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
              "fcb_simulator started: L=%.1fm, dt=%.3fs, u0=%.2f m/s",
              params_.L, cfg_.dt, cfg_.u0);
}

void FcbSimulatorNode::load_params() {
  // Ship particulars
  params_.L = declare_parameter<double>("L", params_.L);
  params_.d = declare_parameter<double>("d", params_.d);
  params_.B = declare_parameter<double>("B", params_.B);
  params_.displacement_t = declare_parameter<double>("displacement_t", params_.displacement_t);
  params_.x_G = declare_parameter<double>("x_G", params_.x_G);

  params_.m_x_prime  = declare_parameter<double>("m_x_prime",  params_.m_x_prime);
  params_.m_y_prime  = declare_parameter<double>("m_y_prime",  params_.m_y_prime);
  params_.J_zz_prime = declare_parameter<double>("J_zz_prime", params_.J_zz_prime);

  params_.X_vv   = declare_parameter<double>("X_vv",   params_.X_vv);
  params_.X_vr   = declare_parameter<double>("X_vr",   params_.X_vr);
  params_.X_rr   = declare_parameter<double>("X_rr",   params_.X_rr);
  params_.X_vvvv = declare_parameter<double>("X_vvvv", params_.X_vvvv);
  params_.Y_v    = declare_parameter<double>("Y_v",    params_.Y_v);
  params_.Y_r    = declare_parameter<double>("Y_r",    params_.Y_r);
  params_.Y_vvv  = declare_parameter<double>("Y_vvv",  params_.Y_vvv);
  params_.Y_vvr  = declare_parameter<double>("Y_vvr",  params_.Y_vvr);
  params_.Y_vrr  = declare_parameter<double>("Y_vrr",  params_.Y_vrr);
  params_.Y_rrr  = declare_parameter<double>("Y_rrr",  params_.Y_rrr);
  params_.N_v    = declare_parameter<double>("N_v",    params_.N_v);
  params_.N_r    = declare_parameter<double>("N_r",    params_.N_r);
  params_.N_vvv  = declare_parameter<double>("N_vvv",  params_.N_vvv);
  params_.N_vvr  = declare_parameter<double>("N_vvr",  params_.N_vvr);
  params_.N_vrr  = declare_parameter<double>("N_vrr",  params_.N_vrr);
  params_.N_rrr  = declare_parameter<double>("N_rrr",  params_.N_rrr);

  params_.G_M   = declare_parameter<double>("G_M",   params_.G_M);
  params_.T_phi = declare_parameter<double>("T_phi", params_.T_phi);

  params_.t_P = declare_parameter<double>("t_P", params_.t_P);
  params_.w_P = declare_parameter<double>("w_P", params_.w_P);
  params_.D_P = declare_parameter<double>("D_P", params_.D_P);
  params_.k_0 = declare_parameter<double>("k_0", params_.k_0);
  params_.k_1 = declare_parameter<double>("k_1", params_.k_1);
  params_.k_2 = declare_parameter<double>("k_2", params_.k_2);

  params_.t_R       = declare_parameter<double>("t_R",       params_.t_R);
  params_.a_H       = declare_parameter<double>("a_H",       params_.a_H);
  params_.x_H_prime = declare_parameter<double>("x_H_prime", params_.x_H_prime);
  params_.x_R_prime = declare_parameter<double>("x_R_prime", params_.x_R_prime);
  params_.gamma_R   = declare_parameter<double>("gamma_R",   params_.gamma_R);
  params_.l_R_prime = declare_parameter<double>("l_R_prime", params_.l_R_prime);
  params_.kappa     = declare_parameter<double>("kappa",     params_.kappa);
  params_.epsilon   = declare_parameter<double>("epsilon",   params_.epsilon);
  params_.A_R       = declare_parameter<double>("A_R",       params_.A_R);
  params_.f_alpha   = declare_parameter<double>("f_alpha",   params_.f_alpha);

  cfg_.dt   = declare_parameter<double>("dt",   cfg_.dt);
  cfg_.x0   = declare_parameter<double>("x0",   cfg_.x0);
  cfg_.y0   = declare_parameter<double>("y0",   cfg_.y0);
  cfg_.psi0 = declare_parameter<double>("psi0", cfg_.psi0);
  cfg_.u0   = declare_parameter<double>("u0",   cfg_.u0);
  cfg_.origin_lat = declare_parameter<double>("origin_lat", cfg_.origin_lat);
  cfg_.origin_lon = declare_parameter<double>("origin_lon", cfg_.origin_lon);
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
  state_ = rk4_step(state_, delta, n, params_, cfg_.dt);
  state_.psi = wrap_pi(state_.psi);
}

void FcbSimulatorNode::publish_own_ship_state() {
  l3_external_msgs::msg::FilteredOwnShipState msg;
  msg.stamp = now();

  // NED → WGS84 (flat earth)
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
