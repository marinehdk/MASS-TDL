#include "m5_tactical_planner/bc_mpc/envelope_computer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mass_l3::m5::bc_mpc {

// ===========================================================================
// Anonymous helpers
// ===========================================================================
namespace {

constexpr double kDegToRad = M_PI / 180.0;
constexpr double kMinHorizon_s = 0.001;    // Floor for horizon to avoid /0
constexpr double kEpsilonRad = 1.0e-12;    // Near-zero heading change tolerance
constexpr std::size_t kSectorFanSteps = 8; // Discretisation steps for the sector arc

// Clamp a value to [lo, hi].
inline double clamp(double val, double lo, double hi) {
  return (val < lo) ? lo : ((val > hi) ? hi : val);
}

}  // namespace

// ===========================================================================
// Constructor
// ===========================================================================
EnvelopeComputer::EnvelopeComputer(const Config& cfg) : cfg_(cfg) {}

// ===========================================================================
// max_heading_change_rad
// ===========================================================================
// Models the ship's heading response as a trapezoidal velocity profile:
//   1. Yaw acceleration phase: r(t) ramps from current_r to rot_max at
//      r_dot_max = rudder_slew_deg_s * pi/180 [rad/s^2].
//   2. Steady turn phase: r(t) = rot_max.
// The total heading change is the integral of r(t) over horizon_s.
//
// This is a conservative model: it assumes the rudder must physically slew
// before full turn authority is available.  The MMG oracle would use the
// actual Nomoto K/T parameters; this simplified model is an independent,
// parameterised approximation that does NOT depend on the solver.
double EnvelopeComputer::max_heading_change_rad(
    double current_r_rad_s,
    double rot_max_rad_s,
    double horizon_s) const {
  if (horizon_s <= 0.0) {
    return 0.0;
  }

  // Yaw acceleration limit from rudder slew [rad/s^2].
  const double r_dot_max = std::abs(cfg_.rudder_slew_deg_s) * kDegToRad;

  if (r_dot_max <= 0.0 || rot_max_rad_s <= 0.0) {
    // No turn authority — heading change is just current_r * horizon.
    return std::abs(current_r_rad_s) * horizon_s;
  }

  // We are computing the MAXIMUM additional heading change (in either
  // direction).  The initial yaw rate may already be nonzero; the rudder
  // can continue to accelerate r toward rot_max or -rot_max.  Conservatively,
  // compute the achievable heading change starting from |current_r| toward
  // +rot_max (symmetric for port).  The absolute heading change is what
  // matters for the envelope sector width.

  const double r0 = std::abs(current_r_rad_s);
  const double r_target = rot_max_rad_s;

  // Time to accelerate from r0 to r_target at r_dot_max.
  // If r0 >= r_target, we are already at max turn rate — no ramp needed.
  const double t_ramp = (r_target > r0)
      ? ((r_target - r0) / r_dot_max)
      : 0.0;

  double total_dpsi = 0.0;

  if (t_ramp >= horizon_s) {
    // Entire horizon is in the ramp phase: r(t) = r0 + r_dot_max * t.
    // dpsi = r0 * H + 0.5 * r_dot_max * H^2.
    total_dpsi = r0 * horizon_s + 0.5 * r_dot_max * horizon_s * horizon_s;
  } else {
    // Ramp phase: dpsi_ramp = r0 * t_ramp + 0.5 * r_dot_max * t_ramp^2.
    const double dpsi_ramp = r0 * t_ramp + 0.5 * r_dot_max * t_ramp * t_ramp;
    // Steady phase: r = r_target, for (horizon_s - t_ramp) seconds.
    const double t_steady = horizon_s - t_ramp;
    const double dpsi_steady = r_target * t_steady;
    total_dpsi = dpsi_ramp + dpsi_steady;
  }

  return total_dpsi;
}

// ===========================================================================
// build_sector_envelope
// ===========================================================================
// Builds a polygon representing the reachable footprint given:
//   max_dpsi_rad:   maximum heading deviation [rad] from centreline (symmetric)
//   max_forward_m:  distance travelled at current speed (no decel)
//   min_forward_m:  distance travelled at max-decelerated speed
//   sigma_pos_m:    position uncertainty buffer (NOT applied to polygon;
//                   is_target_inside_envelope handles expansion separately)
//
// Polygon vertices (body frame, x=forward, y=starboard):
//   - Rear edge: (min_forward_m, ±epsilon)
//   - Front arc: sampled points along the max_dpsi sector at max_forward_m
// The polygon is the convex hull of reachable positions WITHOUT sigma expansion.
//
// The sector half-angle is capped at π/2: beyond 90° the ship can turn broadside
// and the "forward sector" model degrades.  When the true max_dpsi exceeds π/2,
// the envelope is already a forward half-plane which is sufficient for the
// conservative reachability check (further widening does not change the polygon
// shape materially).
EnvelopeRegion EnvelopeComputer::build_sector_envelope(
    double max_dpsi_rad,
    double max_forward_m,
    double min_forward_m,
    double sigma_pos_m) const {
  EnvelopeRegion region;
  region.max_lateral_stbd_m = 0.0;
  region.max_lateral_port_m = 0.0;
  region.max_forward_m = max_forward_m;
  region.min_forward_m = min_forward_m;

  // Guard: zero or negative forward reach → envelope is a circle at origin.
  if (max_forward_m <= kMinHorizon_s && min_forward_m <= kMinHorizon_s) {
    // At-rest case: the envelope is a circle of radius sigma_pos.
    // Build an approximate circle polygon.
    constexpr std::size_t kCircleSteps = 12;
    for (std::size_t i = 0; i < kCircleSteps; ++i) {
      const double angle = 2.0 * M_PI * static_cast<double>(i)
                         / static_cast<double>(kCircleSteps);
      region.polygon.emplace_back(
          sigma_pos_m * std::cos(angle),
          sigma_pos_m * std::sin(angle));
    }
    region.max_lateral_stbd_m = sigma_pos_m;
    region.max_lateral_port_m = -sigma_pos_m;
    region.max_forward_m = sigma_pos_m;
    region.min_forward_m = -sigma_pos_m;
    return region;
  }

  // Clamp min_forward to >= 0.
  min_forward_m = std::max(0.0, min_forward_m);

  // Cap the sector half-angle at π/2.  Beyond 90° the ship can turn broadside;
  // the forward-sector model would produce sin(half_angle) < 0 (wrapped) and the
  // polygon would invert starboard/port.  At π/2 the envelope covers the full
  // forward half-plane, which is the conservative bound we need.
  constexpr double kMaxSectorHalfAngleRad = M_PI / 2.0;
  const double effective_half_angle = std::min(max_dpsi_rad, kMaxSectorHalfAngleRad);

  // Polygon: anchored at origin (current ship position).  The ship starts here
  // and travels forward; every position between 0 and max_forward_m is reachable.
  // Rear edge is at x=0; front arc is at x=max_forward_m.
  const double front_x = max_forward_m;
  const double rear_x  = 0.0;

  // Small epsilon for rear lateral extent (polygon needs non-zero width).
  constexpr double kRearEpsilon = 0.01;
  region.polygon.emplace_back(rear_x, -kRearEpsilon);    // rear left (port)
  region.polygon.emplace_back(rear_x,  kRearEpsilon);    // rear right (starboard)

  // Front arc: sample points from -effective_half_angle to +effective_half_angle.
  for (std::size_t i = 0; i <= kSectorFanSteps; ++i) {
    const double frac = static_cast<double>(i) / static_cast<double>(kSectorFanSteps);
    const double angle = -effective_half_angle
                       + 2.0 * effective_half_angle * frac;
    region.polygon.emplace_back(
        front_x * std::cos(angle),
        front_x * std::sin(angle));
  }

  // Convenience accessors using capped half-angle (always in [0, π/2], sin >= 0).
  const double lateral_at_front = front_x * std::sin(effective_half_angle);
  region.max_lateral_stbd_m = lateral_at_front;
  region.max_lateral_port_m = -lateral_at_front;

  (void)sigma_pos_m;  // Used only in callers (is_target_inside_envelope).
  (void)min_forward_m; // Reserved: min decel reach for future envelope refinement.
  return region;
}

// ===========================================================================
// compute_envelope
// ===========================================================================
EnvelopeRegion EnvelopeComputer::compute_envelope(
    const TrajectoryPoint& own_ship,
    double rot_max_rad_s,
    double decel_max_mps2,
    double dt_s,
    double horizon_s) const {
  // Validate inputs.
  if (horizon_s <= 0.0 || dt_s <= 0.0) {
    EnvelopeRegion empty;
    empty.empty = true;
    return empty;
  }

  const double h = std::max(horizon_s, kMinHorizon_s);
  const double speed = std::max(0.0, own_ship.u_mps);

  // --- Account for take-over latency: the ship continues straight for
  //     cfg_.takeover_latency_s before any manoeuvre begins.
  const double latency = std::max(0.0, cfg_.takeover_latency_s);
  const double effective_horizon_s = std::max(kMinHorizon_s, h - latency);

  // Distance travelled during latency (straight ahead).
  const double latency_dist_m = speed * std::min(latency, h);

  // --- Maximum forward reach: travel at current speed for effective horizon
  //     (conservative — assumes no deceleration).
  const double max_forward_m = speed * effective_horizon_s + latency_dist_m;

  // --- Minimum forward reach: decelerate at max rate for effective horizon.
  //     v(t) = max(0, speed - decel_max * t).  Distance = integral of v(t).
  const double decel = std::max(0.0, decel_max_mps2);
  const double time_to_stop = (decel > 0.0) ? (speed / decel) : 0.0;
  double min_forward_m = latency_dist_m;
  if (decel > 0.0) {
    if (time_to_stop >= effective_horizon_s) {
      // Ship does not fully stop within horizon.
      const double v_end = speed - decel * effective_horizon_s;
      min_forward_m += 0.5 * (speed + v_end) * effective_horizon_s;
    } else {
      // Ship stops within horizon, then stays stopped.
      min_forward_m += 0.5 * speed * time_to_stop;  // triangular area
    }
  } else {
    min_forward_m = max_forward_m;  // Cannot decelerate.
  }

  // --- Maximum heading change (symmetric port/starboard).
  const double max_dpsi = max_heading_change_rad(
      own_ship.r_rad_s, rot_max_rad_s, effective_horizon_s);

  // Build the sector envelope.
  EnvelopeRegion region = build_sector_envelope(
      max_dpsi, max_forward_m, min_forward_m, cfg_.sigma_pos_m);

  // --- Compute convenience accessors in world frame relative offsets.
  //     (Body-frame extents already stored in region by build_sector_envelope.)

  // Detect empty: envelope polygon has fewer than 3 vertices (degenerate).
  if (region.polygon.size() < 3) {
    region.empty = true;
  }

  (void)dt_s;  // Reserved for future step-by-step propagation.
  return region;
}

// ===========================================================================
// is_target_inside_envelope
// ===========================================================================
// Transforms the target position into the ship body frame, then checks
// whether the target (with its own uncertainty radius sigma_pos_m) overlaps
// with the envelope polygon.
//
// Uses a simple point-in-polygon test with the sigma_pos_m expansion
// (conservative: treated as expanding the envelope by sigma_pos_m, and the
//  target is treated as a point).
bool EnvelopeComputer::is_target_inside_envelope(
    const EnvelopeRegion& envelope,
    const TargetState& target,
    const TrajectoryPoint& own_ship,
    double sigma_pos_m) const {
  if (envelope.empty) {
    // Empty envelope means NO feasible manoeuvre — everything is "inside"
    // in the sense that the ship cannot avoid anything.
    return true;
  }

  if (envelope.polygon.size() < 3) {
    return true;  // Degenerate envelope — cannot guarantee avoidance.
  }

  // --- Compute target position relative to own ship in body frame.
  //     World frame: x = north, y = east.
  //     Body frame:  x = forward (along own heading), y = starboard.
  const double dx_world = target.x_m - own_ship.x_m;
  const double dy_world = target.y_m - own_ship.y_m;
  const double cos_psi = std::cos(own_ship.psi_rad);
  const double sin_psi = std::sin(own_ship.psi_rad);
  // Rotate world->body: [x_body] = [ cos_psi  sin_psi] [dx_world]
  //                      [y_body]   [-sin_psi  cos_psi] [dy_world]
  // Wait — let's re-derive.
  // North = cos(psi)*forward + (-sin(psi))*starboard.
  // East  = sin(psi)*forward + cos(psi)*starboard.
  // So: forward  =  cos(psi)*dx + sin(psi)*dy
  //     starboard = -sin(psi)*dx + cos(psi)*dy
  const double tgt_x_body =  cos_psi * dx_world + sin_psi * dy_world;
  const double tgt_y_body = -sin_psi * dx_world + cos_psi * dy_world;

  // --- Expand envelope by sigma_pos_m (target uncertainty).
  //     We perform an outward offset of the polygon, then use ray-casting
  //     point-in-polygon.  For a convex polygon a simpler check is possible,
  //     but we implement the full ray-cast for correctness with any shape.
  //     The expansion is done implicitly: we check the target point against
  //     the original polygon, but with a tolerance of sigma_pos_m.  If the
  //     minimum distance from the target point to any polygon edge is less
  //     than sigma_pos_m, we consider the target inside.
  const double sigma = std::max(0.0, sigma_pos_m);

  // --- Point-in-polygon via ray-casting (with expansion).
  //     First, do a standard point-in-polygon test (target as point).
  const auto& poly = envelope.polygon;
  const std::size_t n = poly.size();

  auto point_in_polygon = [](double px, double py,
                              const std::vector<Eigen::Vector2d>& p) -> bool {
    const std::size_t np = p.size();
    bool inside = false;
    for (std::size_t i = 0, j = np - 1; i < np; j = i++) {
      const double xi = p[i].x();
      const double yi = p[i].y();
      const double xj = p[j].x();
      const double yj = p[j].y();
      if (((yi > py) != (yj > py))
          && (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) {
        inside = !inside;
      }
    }
    return inside;
  };

  if (point_in_polygon(tgt_x_body, tgt_y_body, poly)) {
    return true;
  }

  // --- If not strictly inside, check distance to edges.
  //     If the target is within sigma of any edge, consider it inside.
  auto point_to_segment_dist = [](
      double px, double py, double ax, double ay, double bx, double by) -> double {
    const double abx = bx - ax;
    const double aby = by - ay;
    const double apx = px - ax;
    const double apy = py - ay;
    const double ab2 = abx * abx + aby * aby;
    if (ab2 < 1.0e-12) {
      // Degenerate segment (a == b): distance to point.
      return std::hypot(px - ax, py - ay);
    }
    double t = (apx * abx + apy * aby) / ab2;
    t = clamp(t, 0.0, 1.0);
    const double proj_x = ax + t * abx;
    const double proj_y = ay + t * aby;
    return std::hypot(px - proj_x, py - proj_y);
  };

  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t j = (i + 1) % n;
    const double dist = point_to_segment_dist(
        tgt_x_body, tgt_y_body,
        poly[i].x(), poly[i].y(),
        poly[j].x(), poly[j].y());
    if (dist <= sigma) {
      return true;
    }
  }

  return false;
}

}  // namespace mass_l3::m5::bc_mpc
