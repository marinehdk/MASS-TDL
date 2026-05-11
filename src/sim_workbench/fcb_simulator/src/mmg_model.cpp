// SPDX-License-Identifier: Proprietary
// 4-DOF MMG equations of motion — Yasukawa & Yoshimura 2015 [R7].
#include "fcb_simulator/mmg_model.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace fcb_sim {

namespace {

constexpr double kVelocityFloor = 1e-6;  // m/s, divide-by-zero guard for U
constexpr double kPi = 3.14159265358979323846;

// Compute hull hydrodynamic forces in body frame.
// Returns {X_H, Y_H, N_H} (N, N, N·m).
struct HullForces { double X; double Y; double N; };

HullForces compute_hull_forces(double u, double v, double r, const MmgParams& p) {
  const double U = std::max(std::sqrt(u * u + v * v), kVelocityFloor);
  const double v_p = v / U;
  const double r_p = r * p.L / U;

  const double X_nd = p.X_vv * v_p * v_p
                    + p.X_vr * v_p * r_p
                    + p.X_rr * r_p * r_p
                    + p.X_vvvv * v_p * v_p * v_p * v_p;
  const double Y_nd = p.Y_v * v_p
                    + p.Y_r * r_p
                    + p.Y_vvv * v_p * v_p * v_p
                    + p.Y_vvr * v_p * v_p * r_p
                    + p.Y_vrr * v_p * r_p * r_p
                    + p.Y_rrr * r_p * r_p * r_p;
  const double N_nd = p.N_v * v_p
                    + p.N_r * r_p
                    + p.N_vvv * v_p * v_p * v_p
                    + p.N_vvr * v_p * v_p * r_p
                    + p.N_vrr * v_p * r_p * r_p
                    + p.N_rrr * r_p * r_p * r_p;

  const double q = 0.5 * p.rho * p.L * p.d * U * U;
  HullForces hf;
  hf.X = q * X_nd;
  hf.Y = q * Y_nd;
  hf.N = q * p.L * N_nd;
  return hf;
}

// Propeller thrust X_P (N).
double compute_thrust(double u, double n_rps, const MmgParams& p) {
  if (std::abs(n_rps) < 1e-3) {
    return 0.0;
  }
  const double J = u * (1.0 - p.w_P) / (n_rps * p.D_P);
  const double K_T = p.k_2 * J * J + p.k_1 * J + p.k_0;
  return (1.0 - p.t_P) * p.rho * n_rps * n_rps
         * std::pow(p.D_P, 4) * K_T;
}

struct RudderForces { double X; double Y; double N; };

RudderForces compute_rudder_forces(double u, double v, double r,
                                   double delta, double n_rps,
                                   const MmgParams& p) {
  const double U = std::max(std::sqrt(u * u + v * v), kVelocityFloor);
  const double beta = std::atan2(-v, std::max(u, kVelocityFloor));
  const double r_p = r * p.L / U;

  // Effective inflow at rudder
  const double v_R = U * p.gamma_R * (beta - p.l_R_prime * r_p);

  // C_Th — propeller thrust loading coefficient
  double C_Th = 0.0;
  if (std::abs(n_rps) > 1e-3 && std::abs(u) > kVelocityFloor) {
    const double thrust = compute_thrust(u, n_rps, p);
    const double denom = 0.5 * p.rho * (kPi * 0.25 * p.D_P * p.D_P)
                         * u * u;
    C_Th = std::min(thrust / std::max(denom, 1e-6), 20.0);  // cap prevents blow-up below ~0.03 m/s
  }

  const double eta = p.D_P / p.d;  // propeller-to-rudder height ratio (approx)
  const double bracket = eta
      * std::pow(1.0 + p.kappa * (std::sqrt(std::max(1.0 + C_Th, 0.0)) - 1.0), 2)
      + (1.0 - eta);
  const double u_R = p.epsilon * u * std::sqrt(std::max(bracket, 0.0));

  const double U_R = std::sqrt(u_R * u_R + v_R * v_R);
  const double alpha_R = delta - std::atan2(v_R, std::max(u_R, kVelocityFloor));
  const double F_N = 0.5 * p.rho * p.A_R * p.f_alpha * U_R * U_R
                     * std::sin(alpha_R);

  RudderForces rf;
  rf.X = -(1.0 - p.t_R) * F_N * std::sin(delta);
  rf.Y = -(1.0 + p.a_H) * F_N * std::cos(delta);
  rf.N = -(p.x_R_prime + p.a_H * p.x_H_prime) * p.L
         * F_N * std::cos(delta);
  return rf;
}

}  // namespace

FcbState compute_derivatives(const FcbState& s,
                             double delta_rad,
                             double n_rps,
                             const MmgParams& p) {
  // Mass + added mass (dimensional)
  const double mass = p.displacement_t * 1000.0;
  const double half_rho_L2_d = 0.5 * p.rho * p.L * p.L * p.d;
  const double m_x = p.m_x_prime * half_rho_L2_d;
  const double m_y = p.m_y_prime * half_rho_L2_d;
  const double I_zz = mass * std::pow(0.25 * p.L, 2);  // approx radius of gyration L/4
  const double J_zz = p.J_zz_prime * 0.5 * p.rho
                      * std::pow(p.L, 4) * p.d;

  const HullForces hf = compute_hull_forces(s.u, s.v, s.r, p);
  const RudderForces rf = compute_rudder_forces(s.u, s.v, s.r,
                                                delta_rad, n_rps, p);
  const double X_P = compute_thrust(s.u, n_rps, p);

  const double X = hf.X + rf.X + X_P;
  const double Y = hf.Y + rf.Y;
  const double N = hf.N + rf.N;

  // Solve the coupled (v_dot, r_dot) 2x2 system:
  //   (m + m_y) v_dot + m * x_G * r_dot = Y - (m + m_x) u r
  //   m * x_G * v_dot + (I_zz + J_zz) r_dot = N - m * x_G * u r
  const double a11 = mass + m_y;
  const double a12 = mass * p.x_G;
  const double a21 = mass * p.x_G;
  const double a22 = I_zz + J_zz;
  const double rhs1 = Y - (mass + m_x) * s.u * s.r;
  const double rhs2 = N - mass * p.x_G * s.u * s.r;
  const double det = a11 * a22 - a12 * a21;
  if (std::abs(det) < 1e-6) {
    throw std::runtime_error("MMG inertia matrix near-singular (det=" + std::to_string(det) +
                             "); check x_G in YAML");
  }
  const double v_dot = (a22 * rhs1 - a12 * rhs2) / det;
  const double r_dot = (-a21 * rhs1 + a11 * rhs2) / det;

  // Surge:
  //   (m + m_x) u_dot = X + (m + m_y) v r + x_G m r^2
  const double u_dot = (X + (mass + m_y) * s.v * s.r
                        + p.x_G * mass * s.r * s.r) / (mass + m_x);

  // Roll (1-DOF linear pendulum, weakly coupled):
  //   phi_ddot = -omega_n^2 * phi - 2*zeta*omega_n * phi_dot  (linearised 1-DOF; sway coupling deferred to HAZID)
  // G_M exposed in YAML for HAZID calibration; omega_n derived from T_phi only until roll model matures
  const double omega_n = 2.0 * kPi / p.T_phi;
  constexpr double kZeta = 0.05;
  const double phi_ddot = -omega_n * omega_n * s.phi
                          - 2.0 * kZeta * omega_n * s.phi_dot;

  // Kinematics (NED, flat earth):
  //   x_dot = u cos(psi) - v sin(psi)
  //   y_dot = u sin(psi) + v cos(psi)
  //   psi_dot = r
  FcbState d;
  d.x = s.u * std::cos(s.psi) - s.v * std::sin(s.psi);
  d.y = s.u * std::sin(s.psi) + s.v * std::cos(s.psi);
  d.psi = s.r;
  d.u = u_dot;
  d.v = v_dot;
  d.r = r_dot;
  d.phi = s.phi_dot;
  d.phi_dot = phi_ddot;
  return d;
}

}  // namespace fcb_sim
