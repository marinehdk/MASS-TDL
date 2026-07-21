#ifndef MASS_L3_M5_BC_MPC_ENVELOPE_COMPUTER_HPP_
#define MASS_L3_M5_BC_MPC_ENVELOPE_COMPUTER_HPP_

// M5 Tactical Planner — Last-Safe-Maneuver Envelope Computer (L5-T3)
// PATH-D (MISRA C++:2023): [[nodiscard]], no float, no bare new/delete.
//
// Independent reachability computation to define the BC-MPC takeover boundary.
// Does NOT depend on solver convergence — computes a conservative geometric
// envelope from own-ship state + manoeuvring limits. When the solver has no
// solution within the Envelope, BC-MPC take-over is triggered.
//
// Constants marked [TBD-HAZID] must be calibrated during HAZID RUN-001.
// Reference: BL-B escalation MMG oracle conservative factor method.

#include <cstdint>
#include <vector>

#include <Eigen/Dense>

#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5::bc_mpc {

// ===========================================================================
// EnvelopeRegion — conservative reachable footprint (polygon in body frame)
// ===========================================================================
// The polygon vertices describe the boundary of the region the ship cannot
// escape within the horizon, given its maximum turn rate and deceleration
// limits.  The polygon is expressed in the ship's body frame:
//   x = forward (along current heading), y = starboard.
// An empty envelope means NO feasible manoeuvre exists — immediate escalation.
struct EnvelopeRegion {
  // Polygon vertices in ship body frame [m] (x=forward, y=starboard).
  // Closed: first vertex is NOT repeated.
  std::vector<Eigen::Vector2d> polygon;

  // True when no feasible manoeuvre exists within the given constraints.
  // Example: rudder slew + max turn rate cannot achieve any heading change
  // within the horizon, and the target is directly ahead.
  bool empty{false};

  // Maximum lateral reach starboard [m] — convenience accessor.
  double max_lateral_stbd_m{0.0};
  // Maximum lateral reach port [m] (negative or zero).
  double max_lateral_port_m{0.0};
  // Maximum forward reach [m] (no deceleration).
  double max_forward_m{0.0};
  // Minimum forward reach [m] (max deceleration).
  double min_forward_m{0.0};
};

// ===========================================================================
// EnvelopeComputer — independent reachability-based BC-MPC takeover gate
// ===========================================================================
class EnvelopeComputer {
 public:
  struct Config {
    // [TBD-HAZID] Position uncertainty buffer [m].
    // Typical GPS+DR error.  Must be calibrated during HAZID RUN-001.
    double sigma_pos_m{50.0};

    // [TBD-HAZID] Rudder slew rate limit [deg/s].
    // Limits how fast the ship can initiate a turn.  Typical medium vessel.
    // Interpreted as maximum yaw acceleration: r_dot_max = slew_deg_s * pi/180.
    double rudder_slew_deg_s{2.5};

    // [TBD-HAZID] Take-over latency [s].
    // Time between BC-MPC trigger and actual control handover.
    // During this period the ship continues on its current heading/speed.
    double takeover_latency_s{2.0};
  };

  EnvelopeComputer() = default;
  explicit EnvelopeComputer(const Config& cfg);

  // -------------------------------------------------------------------------
  // compute_envelope — build the conservative reachable-region polygon
  // -------------------------------------------------------------------------
  // own_ship:     current own-ship state (position, heading, speed,
  //               yaw rate used as initial condition for rudder slew ramp)
  // rot_max_rad_s: maximum steady-state yaw rate [rad/s]
  // decel_max_mps2: maximum deceleration [m/s^2]
  // dt_s:         time step for polygon discretisation [s]
  // horizon_s:    forward horizon duration [s]
  //
  // Returns the reachable envelope in the ship body frame.
  // An empty polygon with empty==true signals no feasible manoeuvre exists.
  [[nodiscard]] EnvelopeRegion compute_envelope(
      const TrajectoryPoint& own_ship,
      double rot_max_rad_s,
      double decel_max_mps2,
      double dt_s,
      double horizon_s) const;

  // -------------------------------------------------------------------------
  // is_target_inside_envelope — check if a target could enter the envelope
  // -------------------------------------------------------------------------
  // envelope:     computed reachable envelope (from compute_envelope)
  // target:       target state at current time
  // own_ship:     own-ship state (needed for body-to-world transform)
  // sigma_pos_m:  position uncertainty buffer to expand envelope [m]
  //
  // Returns true when the target's projected region overlaps with the
  // expanded envelope — i.e., the ship may not be able to avoid this target
  // within its manoeuvring limits.
  [[nodiscard]] bool is_target_inside_envelope(
      const EnvelopeRegion& envelope,
      const TargetState& target,
      const TrajectoryPoint& own_ship,
      double sigma_pos_m) const;

 private:
  // Compute the maximum achievable heading change [rad] within horizon_s,
  // accounting for rudder slew rate (limits yaw acceleration) and steady-state
  // rot_max.  current_r_rad_s is the initial yaw rate.
  [[nodiscard]] double max_heading_change_rad(
      double current_r_rad_s,
      double rot_max_rad_s,
      double horizon_s) const;

  // Build a conservative sector polygon from the reachable heading envelope.
  [[nodiscard]] EnvelopeRegion build_sector_envelope(
      double max_dpsi_rad,
      double max_forward_m,
      double min_forward_m,
      double sigma_pos_m) const;

  Config cfg_;
};

}  // namespace mass_l3::m5::bc_mpc

#endif  // MASS_L3_M5_BC_MPC_ENVELOPE_COMPUTER_HPP_
