// SPDX-License-Identifier: Proprietary
// FCB simulator core types — Yasukawa & Yoshimura 2015 [R7] MMG model.
#ifndef FCB_SIMULATOR_TYPES_HPP_
#define FCB_SIMULATOR_TYPES_HPP_

#include <Eigen/Core>

namespace fcb_sim {

// State vector: [x, y, psi, u, v, r, phi, phi_dot]
// x, y       : position relative to origin (m); x=East, y=North (ENU flat-earth)
// psi        : heading, math convention (rad, CCW from East; 0=East, π/2=North)
// u, v       : surge / sway through-water velocity (m/s)
// r          : yaw rate (rad/s)
// phi        : roll angle (rad)
// phi_dot    : roll rate (rad/s)
using StateVec = Eigen::Matrix<double, 8, 1>;

struct FcbState {
  double x{0.0};
  double y{0.0};
  double psi{0.0};
  double u{0.0};
  double v{0.0};
  double r{0.0};
  double phi{0.0};
  double phi_dot{0.0};

  StateVec to_vec() const {
    StateVec s;
    s << x, y, psi, u, v, r, phi, phi_dot;
    return s;
  }
  static FcbState from_vec(const StateVec& s) {
    FcbState fs;
    fs.x = s(0); fs.y = s(1); fs.psi = s(2);
    fs.u = s(3); fs.v = s(4); fs.r = s(5);
    fs.phi = s(6); fs.phi_dot = s(7);
    return fs;
  }
};

// MMG parameter set — populated from YAML by the ROS2 node.
struct MmgParams {
  // Ship particulars
  double L{46.0};
  double d{2.8};
  double B{8.0};
  double displacement_t{450.0};
  double x_G{0.0};
  double rho{1025.0};   // seawater density (kg/m^3)
  double g{9.80665};

  // Added mass (non-dimensional)
  double m_x_prime{0.00831};
  double m_y_prime{0.1284};
  double J_zz_prime{0.00676};

  // Hull derivatives (Abkowitz)
  double X_vv{-0.0407};
  double X_vr{ 0.0441};
  double X_rr{ 0.0127};
  double X_vvvv{-0.0607};
  double Y_v{ -0.3073};
  double Y_r{  0.1521};
  double Y_vvv{-0.7256};
  double Y_vvr{-0.1338};
  double Y_vrr{ 0.1657};
  double Y_rrr{-0.0303};
  double N_v{ -0.1084};
  double N_r{ -0.0585};
  double N_vvv{ 0.0040};
  double N_vvr{-0.0498};
  double N_vrr{-0.0151};
  double N_rrr{-0.0061};

  // Roll
  double G_M{1.2};
  double T_phi{5.0};

  // Propeller
  double t_P{0.184};
  double w_P{0.200};
  double D_P{1.5};
  double k_0{0.6};
  double k_1{-0.3};
  double k_2{-0.25};

  // Rudder
  double t_R{0.387};
  double a_H{0.312};
  double x_H_prime{-0.464};
  double x_R_prime{-0.500};
  double gamma_R{0.395};
  double l_R_prime{-0.710};
  double kappa{0.50};
  double epsilon{1.09};
  double A_R{1.65};
  double f_alpha{2.747};
};

struct SimConfig {
  double dt{0.02};
  double x0{0.0};
  double y0{0.0};
  double psi0{1.5708};
  double u0{9.26};
  double origin_lat{30.5};
  double origin_lon{122.0};
};

}  // namespace fcb_sim

#endif  // FCB_SIMULATOR_TYPES_HPP_
