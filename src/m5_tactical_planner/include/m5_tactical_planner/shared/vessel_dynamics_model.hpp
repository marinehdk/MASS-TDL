#ifndef MASS_L3_M5_SHARED_VESSEL_DYNAMICS_MODEL_HPP_
#define MASS_L3_M5_SHARED_VESSEL_DYNAMICS_MODEL_HPP_

// M5 Tactical Planner — VesselDynamicsModel (4-DOF MMG)
// PATH-D (MISRA C++:2023): ≤60 lines per function, CC ≤10.
//
// Simplified 4-DOF MMG equations (Yasukawa & Yoshimura 2015 [R7]).
// "Simplified" means linear hull-force / rudder-force approximations are used
// in place of the full non-linear polynomial expansion.  Exact coefficients
// (C_Y_beta, C_N_beta, etc.) are [TBD-HAZID] pending pool-test measurements.
//
// Key design constraints (Backseat Driver pattern):
//   - No vessel constants inside this class — all coefficients from manifest
//   - step() uses file-local eval_deriv()/advance() RK4 helpers (see .cpp)
//   - compute_accelerations() kept ≤60 lines

#include <cstdint>
#include <tuple>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/shared/capability_manifest.hpp"

namespace mass_l3::m5::shared {

class VesselDynamicsModel {
 public:
  // State is the same as TrajectoryPoint (x, y, psi, u, v, r).
  using State = mass_l3::m5::TrajectoryPoint;

  // Control input: rudder angle [rad] and propeller speed [rev/s].
  struct Input {
    double rudder_rad{0.0};  // positive = starboard (right) deflection
    double rpm_rps{0.0};     // propeller revolutions per second; 0 = no thrust
  };

  explicit VesselDynamicsModel(const CapabilityManifest& manifest);

  // -------------------------------------------------------------------------
  // step() — one RK4 integration step of the 4-DOF MMG equations.
  // @param s0   Current state (x, y, psi, u, v, r, t)
  // @param cmd  Control input (rudder_rad, rpm_rps)
  // @param dt_s Integration timestep [s]; MUST be > 0
  // @returns    New state at t0 + dt_s
  // Pre-condition: dt_s > 0 (enforced by assertion in debug, silently clamped in release)
  // step() uses file-local eval_deriv()/advance() RK4 helpers (see .cpp).
  // -------------------------------------------------------------------------
  [[nodiscard]] State step(const State& s0,
                           const Input& cmd,
                           double dt_s) const;

  // -------------------------------------------------------------------------
  // rot_max_rad_s() — speed- and sea-state-dependent ROT limit
  //   f(u, Hs) = rot_max_at_18kn * speed_correction(u) * (1 - k_Hs * Hs)
  // @param u_mps  Current surge speed [m/s]
  // @param hs_m   Significant wave height [m]; 0 = calm sea
  // -------------------------------------------------------------------------
  [[nodiscard]] double rot_max_rad_s(double u_mps, double hs_m) const noexcept;

  // -------------------------------------------------------------------------
  // stopping_distance_m() — distance to stop from speed u
  // Linear interpolation between (0 m/s → 0 m) and (service_speed → d_stop).
  // [TBD-HAZID] stopping_distance_at_18kn_m: from ahead-to-astern sea trial.
  // -------------------------------------------------------------------------
  [[nodiscard]] double stopping_distance_m(double u_mps) const noexcept;

  // -------------------------------------------------------------------------
  // compute_accelerations() — exposed for use by file-local RK4 eval helpers.
  // MMG acceleration terms (du/dt, dv/dt, dr/dt) given state + input.
  // Simplified linear model: exact non-linear MMG awaits pool-test data [TBD-HAZID].
  // Must stay ≤ 60 lines (PATH-D constraint).
  // -------------------------------------------------------------------------
  [[nodiscard]] std::tuple<double, double, double> compute_accelerations(
      const State& s, const Input& cmd) const noexcept;

 private:
  CapabilityManifest manifest_;

  // Speed correction factor — linear interp between (low_speed, factor) and
  // (high_speed, factor) nodes from CapabilityManifest.
  [[nodiscard]] double speed_correction_factor(double u_mps) const noexcept;

  // Compute moment of inertia I_zz from mass and geometry.
  // Approximated as (1/12) * m * L^2 (thin rod approximation — [TBD-HAZID]).
  [[nodiscard]] double compute_izz() const noexcept;
};

}  // namespace mass_l3::m5::shared

#endif  // MASS_L3_M5_SHARED_VESSEL_DYNAMICS_MODEL_HPP_
