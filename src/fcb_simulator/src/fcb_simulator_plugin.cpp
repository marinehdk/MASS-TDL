// SPDX-License-Identifier: Proprietary
#include "fcb_simulator/fcb_simulator_plugin.hpp"
#include "fcb_simulator/rk4_integrator.hpp"

#include <yaml-cpp/yaml.h>
#include <pluginlib/class_list_macros.hpp>

namespace fcb_sim {

void FCBSimulator::load_params(const std::string& yaml_path) {
  YAML::Node root = YAML::LoadFile(yaml_path);
  auto p = root["fcb_simulator"]["ros__parameters"];

  #define LOAD(field, key) if (p[key]) params_.field = p[key].as<double>()
  LOAD(L, "L"); LOAD(d, "d"); LOAD(B, "B");
  LOAD(displacement_t, "displacement_t"); LOAD(x_G, "x_G");
  LOAD(m_x_prime, "m_x_prime"); LOAD(m_y_prime, "m_y_prime"); LOAD(J_zz_prime, "J_zz_prime");
  LOAD(X_vv, "X_vv"); LOAD(X_vr, "X_vr"); LOAD(X_rr, "X_rr"); LOAD(X_vvvv, "X_vvvv");
  LOAD(Y_v, "Y_v"); LOAD(Y_r, "Y_r"); LOAD(Y_vvv, "Y_vvv"); LOAD(Y_vvr, "Y_vvr");
  LOAD(Y_vrr, "Y_vrr"); LOAD(Y_rrr, "Y_rrr");
  LOAD(N_v, "N_v"); LOAD(N_r, "N_r"); LOAD(N_vvv, "N_vvv"); LOAD(N_vvr, "N_vvr");
  LOAD(N_vrr, "N_vrr"); LOAD(N_rrr, "N_rrr");
  LOAD(G_M, "G_M"); LOAD(T_phi, "T_phi");
  LOAD(t_P, "t_P"); LOAD(w_P, "w_P"); LOAD(D_P, "D_P");
  LOAD(k_0, "k_0"); LOAD(k_1, "k_1"); LOAD(k_2, "k_2");
  LOAD(t_R, "t_R"); LOAD(a_H, "a_H"); LOAD(x_H_prime, "x_H_prime");
  LOAD(x_R_prime, "x_R_prime"); LOAD(gamma_R, "gamma_R"); LOAD(l_R_prime, "l_R_prime");
  LOAD(kappa, "kappa"); LOAD(epsilon, "epsilon"); LOAD(A_R, "A_R"); LOAD(f_alpha, "f_alpha");
  #undef LOAD
}

ship_sim::ShipState FCBSimulator::step(const ship_sim::ShipState& state,
                                        double delta_rad, double n_rps, double dt_s) {
  FcbState fs;
  fs.x = state.x; fs.y = state.y; fs.psi = state.psi;
  fs.u = state.u; fs.v = state.v; fs.r = state.r;
  fs.phi = state.phi; fs.phi_dot = state.phi_dot;

  fs = rk4_step(fs, delta_rad, n_rps, params_, dt_s);

  ship_sim::ShipState out;
  out.x = fs.x; out.y = fs.y; out.psi = fs.psi;
  out.u = fs.u; out.v = fs.v; out.r = fs.r;
  out.phi = fs.phi; out.phi_dot = fs.phi_dot;
  return out;
}

}  // namespace fcb_sim

PLUGINLIB_EXPORT_CLASS(fcb_sim::FCBSimulator, ship_sim::ShipMotionSimulator)
