#include "m5_tactical_planner/shared/vessel_dynamics_model.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "m5_tactical_planner/common/units.hpp"

namespace mass_l3::m5::shared {

using State = VesselDynamicsModel::State;
using Input = VesselDynamicsModel::Input;

// ===========================================================================
// Constructor
// ===========================================================================
VesselDynamicsModel::VesselDynamicsModel(const CapabilityManifest& manifest)
    : manifest_(manifest) {}

// ===========================================================================
// compute_izz() — approximate moment of inertia (thin rod approximation)
// I_zz ≈ (1/12) * m * L^2. [TBD-HAZID] from inclining experiment.
// ===========================================================================
double VesselDynamicsModel::compute_izz() const noexcept {
  const auto& cfg = manifest_.config();
  return (1.0 / 12.0) * cfg.mass_kg * cfg.length_m * cfg.length_m;
}

// ===========================================================================
// compute_accelerations() — simplified 4-DOF MMG (linear approximation)
// Returns (du_dt, dv_dt, dr_dt).
//
// Simplified linear model; exact non-linear MMG awaits pool-test data.
// [TBD-HAZID] k_prop=500 N·s^2, k_drag=100 N·s^2/m^2: from bollard pull test
// [TBD-HAZID] k_y_rudder=800 N/rad, k_n_rudder=3000 N·m/rad: from rotating-arm
// ===========================================================================
std::tuple<double, double, double>
VesselDynamicsModel::compute_accelerations(const State& s,
                                            const Input& cmd) const noexcept {
  const auto& cfg = manifest_.config();
  const double m      = cfg.mass_kg;
  const double izz    = compute_izz();
  const double m_sge  = m   * (1.0 + cfg.surge_added_mass_factor);
  const double m_swy  = m   * (1.0 + cfg.sway_added_mass_factor);
  const double izz_e  = izz * (1.0 + cfg.yaw_added_inertia_factor);

  constexpr double k_prop     = 500.0;   // [TBD-HAZID]
  constexpr double k_drag     = 100.0;   // [TBD-HAZID]
  constexpr double k_y_rudder = 800.0;   // [TBD-HAZID]
  constexpr double k_n_rudder = 3000.0;  // [TBD-HAZID]

  const double f_prop  = k_prop * cmd.rpm_rps  * std::abs(cmd.rpm_rps);
  const double r_hull  = k_drag * s.u_mps      * std::abs(s.u_mps);
  const double y_rud   = k_y_rudder * s.u_mps * s.u_mps * cmd.rudder_rad;
  const double n_rud   = k_n_rudder * s.u_mps * s.u_mps * cmd.rudder_rad;

  return {(f_prop - r_hull) / m_sge,
          y_rud              / m_swy,
          n_rud              / izz_e};
}

// ===========================================================================
// File-local helpers for RK4 (anonymous namespace = internal linkage)
// Must appear AFTER class member definitions that they reference.
// ===========================================================================
namespace {

// 6-DOF derivative bundle
struct Deriv {
  double dx{0.0}; double dy{0.0}; double dpsi{0.0};
  double du{0.0}; double dv{0.0}; double dr{0.0};
};

// eval_deriv() — collect derivatives at state s
Deriv eval_deriv(const VesselDynamicsModel& mdl,
                 const State& s, const Input& cmd) {
  auto [du, dv, dr] = mdl.compute_accelerations(s, cmd);
  return {s.u_mps * std::cos(s.psi_rad) - s.v_mps * std::sin(s.psi_rad),
          s.u_mps * std::sin(s.psi_rad) + s.v_mps * std::cos(s.psi_rad),
          s.r_rad_s, du, dv, dr};
}

// advance() — apply scaled derivative to a base state
State advance(const State& base, const Deriv& d, double h) {
  State s = base;
  s.x_m += d.dx * h;  s.y_m     += d.dy   * h;
  s.psi_rad += d.dpsi * h;
  s.u_mps += d.du * h; s.v_mps   += d.dv   * h;
  s.r_rad_s += d.dr * h;
  return s;
}

}  // namespace

// ===========================================================================
// step() — RK4 integration of 4-DOF MMG equations (≤60 lines)
// ===========================================================================
State VesselDynamicsModel::step(const State& s0,
                                 const Input& cmd,
                                 double dt_s) const {
  assert(dt_s > 0.0);
  if (dt_s <= 0.0) { return s0; }

  const double h2 = dt_s * 0.5;
  const Deriv k1  = eval_deriv(*this, s0,                   cmd);
  const Deriv k2  = eval_deriv(*this, advance(s0, k1, h2),  cmd);
  const Deriv k3  = eval_deriv(*this, advance(s0, k2, h2),  cmd);
  const Deriv k4  = eval_deriv(*this, advance(s0, k3, dt_s), cmd);

  const double w = dt_s / 6.0;
  const Deriv kb{
      k1.dx   + 2.0*k2.dx   + 2.0*k3.dx   + k4.dx,
      k1.dy   + 2.0*k2.dy   + 2.0*k3.dy   + k4.dy,
      k1.dpsi + 2.0*k2.dpsi + 2.0*k3.dpsi + k4.dpsi,
      k1.du   + 2.0*k2.du   + 2.0*k3.du   + k4.du,
      k1.dv   + 2.0*k2.dv   + 2.0*k3.dv   + k4.dv,
      k1.dr   + 2.0*k2.dr   + 2.0*k3.dr   + k4.dr};

  State sn  = advance(s0, kb, w);
  sn.t_s    = s0.t_s + dt_s;
  return sn;
}

// ===========================================================================
// speed_correction_factor() — linear interpolation between two speed nodes
// ===========================================================================
double VesselDynamicsModel::speed_correction_factor(double u_mps) const noexcept {
  const auto& cfg = manifest_.config();
  const double u_kn = units::mps_to_kn(u_mps);
  if (u_kn <= cfg.low_speed_kn)  { return cfg.low_speed_factor; }
  if (u_kn >= cfg.high_speed_kn) { return cfg.high_speed_factor; }
  const double frac = (u_kn - cfg.low_speed_kn) /
                      (cfg.high_speed_kn - cfg.low_speed_kn);
  return cfg.low_speed_factor +
         frac * (cfg.high_speed_factor - cfg.low_speed_factor);
}

// ===========================================================================
// rot_max_rad_s() — ROT limit: speed + sea-state correction
// f(u, Hs) = rot_max_at_18kn * speed_correction(u) * max(0, 1 - k_Hs * Hs)
// ===========================================================================
double VesselDynamicsModel::rot_max_rad_s(double u_mps,
                                           double hs_m) const noexcept {
  const auto& cfg = manifest_.config();
  const double sf = speed_correction_factor(u_mps);
  const double hf = std::max(0.0, 1.0 - cfg.rough_sea_factor_per_hs * hs_m);
  return cfg.rot_max_at_18kn_rad_s * sf * hf;
}

// ===========================================================================
// stopping_distance_m() — linear interp: 0 m/s → 0 m, service_speed → d_stop
// [TBD-HAZID] stopping_distance_at_18kn_m: from ahead-to-astern sea trial
// ===========================================================================
double VesselDynamicsModel::stopping_distance_m(double u_mps) const noexcept {
  const auto& cfg = manifest_.config();
  const double v_max = units::kn_to_mps(cfg.service_speed_kn);
  if (v_max <= 0.0) { return 0.0; }
  return (std::max(0.0, u_mps) / v_max) * cfg.stopping_distance_at_18kn_m;
}

}  // namespace mass_l3::m5::shared
