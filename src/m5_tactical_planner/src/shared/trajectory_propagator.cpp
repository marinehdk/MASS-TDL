#include "m5_tactical_planner/shared/trajectory_propagator.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>

#include "m5_tactical_planner/common/units.hpp"

namespace {
// [TBD-HAZID] Calibrate from AIS track statistics for FCB operational area (RUN-001).
constexpr double kTurnRate_rad_s = 0.05236;  // 3 deg/s
constexpr double kDecel_mps_s    = 0.1;       // m/s per second
}  // namespace

namespace mass_l3::m5::shared {

using mass_l3::m5::TrajectoryPoint;
using mass_l3::m5::TargetState;
using mass_l3::m5::TargetIntent;

// ---------------------------------------------------------------------------
// clamp_rudder() — saturate rudder at physical deflection limit
// ---------------------------------------------------------------------------
double TrajectoryPropagator::clamp_rudder(double rudder_rad) noexcept {
  return std::max(-kRudderMaxRad, std::min(kRudderMaxRad, rudder_rad));
}

// ---------------------------------------------------------------------------
// compute_tracking_input() — P-controller for heading + speed tracking
// rudder = K_psi * psi_error  (wrapped to (-pi, pi])
// rpm    = K_u   * (u_des - u_current), clamped to [0, ∞)
// [TBD-HAZID] K_psi, K_u: tune from sea-trial step responses (RUN-001).
// ---------------------------------------------------------------------------
VesselDynamicsModel::Input TrajectoryPropagator::compute_tracking_input(
    const TrajectoryPoint& current,
    double psi_des,
    double u_des) const noexcept {
  // Heading error: wrap to (-pi, pi] to avoid ±pi discontinuity
  const double psi_err = units::wrap_pi(psi_des - current.psi_rad);
  const double rudder  = clamp_rudder(kKPsi * psi_err);
  const double rpm     = std::max(0.0, kKU * (u_des - current.u_mps));
  return VesselDynamicsModel::Input{rudder, rpm};
}

// ---------------------------------------------------------------------------
// propagate_own() — N-step RK4 integration of own-ship trajectory
// Returns N+1 points (includes initial state at index 0).
// ---------------------------------------------------------------------------
std::vector<TrajectoryPoint> TrajectoryPropagator::propagate_own(
    const TrajectoryPoint& s0,
    const std::vector<double>& psi_seq,
    const std::vector<double>& u_seq,
    double dt_s,
    const VesselDynamicsModel& dynamics) const {

  assert(psi_seq.size() == u_seq.size());
  assert(!psi_seq.empty());
  assert(dt_s > 0.0);

  const std::size_t n = psi_seq.size();
  std::vector<TrajectoryPoint> traj;
  traj.reserve(n + 1u);
  traj.push_back(s0);

  TrajectoryPoint current = s0;
  for (std::size_t i = 0u; i < n; ++i) {
    const VesselDynamicsModel::Input cmd =
        compute_tracking_input(current, psi_seq[i], u_seq[i]);
    current = dynamics.step(current, cmd, dt_s);
    traj.push_back(current);
  }
  return traj;
}

// ---------------------------------------------------------------------------
// step_target() — one kinematic step under intent scenario
// Returns updated NED (x, y) position; cog/sog evolve per intent:
//   Maintain:        constant course + speed
//   TurnPort:        cog decreases at 3 deg/s (approx)
//   TurnStarboard:   cog increases at 3 deg/s (approx)
//   Decelerate:      speed decreases at 0.1 m/s per step
//   Unknown:         treated as Maintain
//
// [TBD-HAZID] turn rate (3 deg/s) and deceleration (0.1 m/s per step) must be
// calibrated against AIS track statistics from FCB operational area.
// ---------------------------------------------------------------------------
Eigen::Vector2d TrajectoryPropagator::step_target(
    double x, double y, double cog_rad, double sog_mps,
    double dt_s, TargetIntent intent) noexcept {

  double cog   = cog_rad;
  double speed = sog_mps;

  switch (intent) {
    case TargetIntent::TurnPort:
      cog -= kTurnRate_rad_s * dt_s;
      break;
    case TargetIntent::TurnStarboard:
      cog += kTurnRate_rad_s * dt_s;
      break;
    case TargetIntent::Decelerate:
      speed = std::max(0.0, speed - kDecel_mps_s * dt_s);
      break;
    case TargetIntent::Maintain:
    case TargetIntent::Unknown:
    default:
      break;  // constant course + speed
  }

  // NED kinematic propagation
  const double new_x = x + speed * std::cos(cog) * dt_s;
  const double new_y = y + speed * std::sin(cog) * dt_s;
  return Eigen::Vector2d{new_x, new_y};
}

// ---------------------------------------------------------------------------
// propagate_target() — kinematic target prediction
// Returns floor(horizon_s/dt_s)+1 positions (includes initial position).
// ---------------------------------------------------------------------------
std::vector<Eigen::Vector2d> TrajectoryPropagator::propagate_target(
    const TargetState& tgt,
    TargetIntent intent,
    double horizon_s,
    double dt_s) const {

  assert(dt_s > 0.0);
  assert(horizon_s > 0.0);

  const std::size_t n_steps =
      static_cast<std::size_t>(std::floor(horizon_s / dt_s));

  std::vector<Eigen::Vector2d> positions;
  positions.reserve(n_steps + 1u);

  double x   = tgt.x_m;
  double y   = tgt.y_m;
  double cog = tgt.cog_rad;
  double sog = tgt.sog_mps;

  positions.emplace_back(x, y);

  for (std::size_t i = 0u; i < n_steps; ++i) {
    const Eigen::Vector2d next = step_target(x, y, cog, sog, dt_s, intent);
    // Update cog/sog for next step (stateful evolution)
    if (intent == TargetIntent::TurnPort) {
      cog -= kTurnRate_rad_s * dt_s;
    } else if (intent == TargetIntent::TurnStarboard) {
      cog += kTurnRate_rad_s * dt_s;
    } else if (intent == TargetIntent::Decelerate) {
      sog = std::max(0.0, sog - kDecel_mps_s * dt_s);
    }
    x = next.x();
    y = next.y();
    positions.push_back(next);
  }

  return positions;
}

}  // namespace mass_l3::m5::shared
