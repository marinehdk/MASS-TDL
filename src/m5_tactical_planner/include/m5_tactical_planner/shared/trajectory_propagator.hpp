#ifndef MASS_L3_M5_SHARED_TRAJECTORY_PROPAGATOR_HPP_
#define MASS_L3_M5_SHARED_TRAJECTORY_PROPAGATOR_HPP_

// M5 Tactical Planner — TrajectoryPropagator
// PATH-D (MISRA C++:2023): [[nodiscard]], ≤60 lines per function, CC ≤10.
//
// Two propagation modes:
//   1. Own-ship: RK4 integration via VesselDynamicsModel, tracking heading+speed
//      sequences using P-controller to generate (rudder, rpm) commands.
//   2. Target:   Kinematic prediction under a given IntentScenario (constant
//      course, port/starboard turn, decelerate).  No dynamics model needed
//      for targets (track-level prediction only).

#include <cstdint>
#include <vector>

#include <Eigen/Dense>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/shared/vessel_dynamics_model.hpp"

namespace mass_l3::m5::shared {

class TrajectoryPropagator {
 public:
  // -------------------------------------------------------------------------
  // propagate_own() — integrate own-ship forward N steps
  // @param s0       Initial state
  // @param psi_seq  Sequence of N desired headings [rad] (length N)
  // @param u_seq    Sequence of N desired surge speeds [m/s] (length N)
  // @param dt_s     Integration timestep [s]
  // @param dynamics VesselDynamicsModel providing step()
  // @returns        Vector of N+1 TrajectoryPoints (includes s0 at index 0)
  // Pre-condition: psi_seq.size() == u_seq.size() == N ≥ 1
  // -------------------------------------------------------------------------
  [[nodiscard]] std::vector<mass_l3::m5::TrajectoryPoint> propagate_own(
      const mass_l3::m5::TrajectoryPoint& s0,
      const std::vector<double>& psi_seq,
      const std::vector<double>& u_seq,
      double dt_s,
      const VesselDynamicsModel& dynamics) const;

  // -------------------------------------------------------------------------
  // propagate_target() — predict target (x, y) positions under intent
  // @param tgt       Current target state
  // @param intent    Intent scenario (Maintain, TurnPort, TurnStarboard, Decelerate)
  // @param horizon_s Total prediction horizon [s]
  // @param dt_s      Timestep [s]
  // @returns         Vector of floor(horizon_s/dt_s)+1 (x,y) positions
  //                  including the initial position at index 0
  // -------------------------------------------------------------------------
  [[nodiscard]] std::vector<Eigen::Vector2d> propagate_target(
      const mass_l3::m5::TargetState& tgt,
      mass_l3::m5::TargetIntent intent,
      double horizon_s,
      double dt_s) const;

 private:
  // P-controller constants for tracking desired (heading, speed).
  // [TBD-HAZID] K_psi, K_u: tune from sea trial step responses.
  // These are intentionally small to avoid overshoot in the simplified model.
  static constexpr double kKPsi = 0.5;  // rudder gain [rad / rad error]
  static constexpr double kKU   = 2.0;  // rpm gain [(rev/s) / (m/s error)]

  // Max rudder deflection [rad] — physical limit, not [TBD-HAZID].
  static constexpr double kRudderMaxRad = 0.6109;  // 35 deg in radians

  // Compute tracking (rudder, rpm) input via P-controller.
  [[nodiscard]] VesselDynamicsModel::Input compute_tracking_input(
      const mass_l3::m5::TrajectoryPoint& current,
      double psi_des,
      double u_des) const noexcept;

  // Clamp rudder to physical limits.
  [[nodiscard]] static double clamp_rudder(double rudder_rad) noexcept;

  // Apply intent to evolve target position by one dt step.
  // cog_rad and sog_mps are mutated in place (evolve under intent).
  // Returns updated (x, y) position.
  // Single evolution code path — propagate_target() calls this iteratively.
  [[nodiscard]] static Eigen::Vector2d step_target(
      double x, double y, double& cog_rad, double& sog_mps, double dt_s,
      mass_l3::m5::TargetIntent intent) noexcept;
};

}  // namespace mass_l3::m5::shared

#endif  // MASS_L3_M5_SHARED_TRAJECTORY_PROPAGATOR_HPP_
