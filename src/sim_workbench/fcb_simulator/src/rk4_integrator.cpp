// SPDX-License-Identifier: Proprietary
// Classic 4th-order Runge-Kutta over the 8-state FCB MMG dynamics.
#include "fcb_simulator/rk4_integrator.hpp"

#include "fcb_simulator/mmg_model.hpp"

namespace fcb_sim {

namespace {

inline FcbState add(const FcbState& a, const FcbState& b, double s) {
  FcbState r;
  r.x       = a.x       + s * b.x;
  r.y       = a.y       + s * b.y;
  r.psi     = a.psi     + s * b.psi;
  r.u       = a.u       + s * b.u;
  r.v       = a.v       + s * b.v;
  r.r       = a.r       + s * b.r;
  r.phi     = a.phi     + s * b.phi;
  r.phi_dot = a.phi_dot + s * b.phi_dot;
  return r;
}

}  // namespace

FcbState rk4_step(const FcbState& state,
                  double delta_rad,
                  double n_rps,
                  const MmgParams& p,
                  double dt) {
  const FcbState k1 = compute_derivatives(state, delta_rad, n_rps, p);
  const FcbState k2 = compute_derivatives(add(state, k1, 0.5 * dt),
                                          delta_rad, n_rps, p);
  const FcbState k3 = compute_derivatives(add(state, k2, 0.5 * dt),
                                          delta_rad, n_rps, p);
  const FcbState k4 = compute_derivatives(add(state, k3, dt),
                                          delta_rad, n_rps, p);

  FcbState out;
  out.x       = state.x       + (dt / 6.0) * (k1.x       + 2.0 * k2.x       + 2.0 * k3.x       + k4.x);
  out.y       = state.y       + (dt / 6.0) * (k1.y       + 2.0 * k2.y       + 2.0 * k3.y       + k4.y);
  out.psi     = state.psi     + (dt / 6.0) * (k1.psi     + 2.0 * k2.psi     + 2.0 * k3.psi     + k4.psi);
  out.u       = state.u       + (dt / 6.0) * (k1.u       + 2.0 * k2.u       + 2.0 * k3.u       + k4.u);
  out.v       = state.v       + (dt / 6.0) * (k1.v       + 2.0 * k2.v       + 2.0 * k3.v       + k4.v);
  out.r       = state.r       + (dt / 6.0) * (k1.r       + 2.0 * k2.r       + 2.0 * k3.r       + k4.r);
  out.phi     = state.phi     + (dt / 6.0) * (k1.phi     + 2.0 * k2.phi     + 2.0 * k3.phi     + k4.phi);
  out.phi_dot = state.phi_dot + (dt / 6.0) * (k1.phi_dot + 2.0 * k2.phi_dot + 2.0 * k3.phi_dot + k4.phi_dot);
  return out;
}

}  // namespace fcb_sim
